#include "CatLink.h"

#include "CatTransport.h"

#include <algorithm>

namespace ts590 {

namespace {

// Longer than any legitimate answer; protects against a noisy line.
constexpr qsizetype kMaxFrameLength = 256;

// Answers begin with an upper-case command name, or '?' for an error. Plain
// ASCII on purpose: std::isalpha is locale-dependent for bytes >= 0x80.
bool isFrameStart(char c)
{
    return (c >= 'A' && c <= 'Z') || c == '?';
}

bool isErrorFrame(const QByteArray &frame)
{
    return frame == "?" || frame == "E" || frame == "O";
}

}   // namespace

CatLink::CatLink(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &CatLink::onTimer);
}

CatLink::~CatLink() = default;

void CatLink::setTransport(std::unique_ptr<CatTransport> transport)
{
    clear();
    m_timer.stop();
    m_inFlight.reset();
    m_rxBuffer.clear();
    if (m_transport) {
        m_transport->disconnect(this);
        // May be called from inside the transport's own signal; don't delete it under itself.
        m_transport.release()->deleteLater();
    }
    m_transport = std::move(transport);
    if (m_transport)
        connect(m_transport.get(), &CatTransport::dataReceived, this, &CatLink::onData);
}

bool CatLink::isOpen() const
{
    return m_transport && m_transport->isOpen();
}

void CatLink::send(const QByteArray &command, const QByteArray &coalesceKey)
{
    if (!coalesceKey.isEmpty()) {
        auto it = std::find_if(m_queue.begin(), m_queue.end(), [&](const Pending &p) {
            return p.replyPrefix.isEmpty() && p.key == coalesceKey;
        });
        if (it != m_queue.end()) {
            it->command = command;
            return;
        }
    }
    m_queue.push_back({command, {}, coalesceKey});
    pump();
}

void CatLink::query(const QByteArray &command, const QByteArray &replyPrefix)
{
    // Collapse into an identical queued query, but only if no set is queued
    // after it: a read requested after a write must observe that write.
    for (auto it = m_queue.rbegin(); it != m_queue.rend(); ++it) {
        if (it->replyPrefix.isEmpty())
            break;
        if (it->command == command)
            return;
    }
    QByteArray prefix = replyPrefix.isEmpty() ? command.chopped(1) : replyPrefix;
    m_queue.push_back({command, prefix, command});
    pump();
}

void CatLink::clear()
{
    m_queue.clear();
}

int CatLink::pendingCount() const
{
    return int(m_queue.size()) + (m_inFlight ? 1 : 0);
}

void CatLink::pump()
{
    if (m_inFlight || m_queue.empty() || !isOpen())
        return;
    m_inFlight = std::move(m_queue.front());
    m_queue.pop_front();

    m_transport->write(m_inFlight->command);
    emit frameSent(m_inFlight->command);

    const int wire = m_transport->transmitTimeMs(m_inFlight->command.size());
    const bool isQuery = !m_inFlight->replyPrefix.isEmpty();
    m_timer.start(wire + (isQuery ? m_replyTimeoutMs : m_commandGapMs));
}

void CatLink::onTimer()
{
    if (!m_inFlight)
        return;
    const Pending done = std::move(*m_inFlight);
    m_inFlight.reset();
    if (!done.replyPrefix.isEmpty())
        emit queryTimedOut(done.command);
    pump();
}

void CatLink::onData(const QByteArray &data)
{
    m_rxBuffer += data;
    qsizetype end;
    while ((end = m_rxBuffer.indexOf(';')) >= 0) {
        QByteArray frame = m_rxBuffer.left(end);
        m_rxBuffer.remove(0, end + 1);
        // Line noise or a power-on glitch can leave junk ahead of a frame.
        qsizetype start = 0;
        while (start < frame.size() && !isFrameStart(frame[start]))
            ++start;
        frame.remove(0, start);
        if (!frame.isEmpty())
            handleFrame(frame);
    }
    if (m_rxBuffer.size() > kMaxFrameLength)
        m_rxBuffer.clear();
}

void CatLink::handleFrame(const QByteArray &frame)
{
    if (isErrorFrame(frame)) {
        QByteArray command;
        if (m_inFlight) {
            command = m_inFlight->command;
            m_inFlight.reset();
            m_timer.stop();
        }
        emit commandRejected(command, frame[0]);
        pump();
        return;
    }

    const bool completesQuery = m_inFlight && !m_inFlight->replyPrefix.isEmpty()
                                && frame.startsWith(m_inFlight->replyPrefix);
    if (completesQuery) {
        m_inFlight.reset();
        m_timer.stop();
    }
    emit frameReceived(frame);
    if (completesQuery)
        pump();
}

}   // namespace ts590
