#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>

#include <deque>
#include <memory>
#include <optional>

namespace ts590 {

class CatTransport;

// Frames the radio's ';'-terminated byte stream and serializes outgoing
// commands. The radio's error answers ("?;", "E;", "O;") carry no command
// name, so commands are sent one at a time: a query waits for its answer (or
// a timeout), a set waits a short gap. That way an error can be attributed to
// the command that caused it.
//
// Queued commands that share a key replace each other, so dragging a slider
// sends the latest value instead of a backlog.
class CatLink : public QObject
{
    Q_OBJECT

public:
    explicit CatLink(QObject *parent = nullptr);
    ~CatLink() override;

    // Takes ownership. Passing nullptr releases the current transport.
    void setTransport(std::unique_ptr<CatTransport> transport);
    CatTransport *transport() const { return m_transport.get(); }
    bool isOpen() const;

    // Fire-and-forget set command. An empty key means "never coalesce".
    void send(const QByteArray &command, const QByteArray &coalesceKey = {});
    // Read command; completes when a frame starting with replyPrefix arrives
    // (default: the command without its ';'). Duplicate queued queries collapse.
    void query(const QByteArray &command, const QByteArray &replyPrefix = {});
    // Drops everything not yet sent.
    void clear();
    int pendingCount() const;

    void setReplyTimeout(int ms) { m_replyTimeoutMs = ms; }
    void setCommandGap(int ms) { m_commandGapMs = ms; }

signals:
    void frameSent(const QByteArray &command);
    void frameReceived(const QByteArray &frame);   // without ';'
    // code is '?', 'E' or 'O'. command is empty if nothing was in flight.
    void commandRejected(const QByteArray &command, char code);
    void queryTimedOut(const QByteArray &command);

private:
    struct Pending {
        QByteArray command;
        QByteArray replyPrefix;   // empty for sets
        QByteArray key;
    };

    void onData(const QByteArray &data);
    void handleFrame(const QByteArray &frame);
    void onTimer();
    void pump();

    std::unique_ptr<CatTransport> m_transport;
    QByteArray m_rxBuffer;
    std::deque<Pending> m_queue;
    std::optional<Pending> m_inFlight;
    QTimer m_timer;
    int m_replyTimeoutMs = 500;
    int m_commandGapMs = 10;
};

}   // namespace ts590
