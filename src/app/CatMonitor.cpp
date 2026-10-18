#include "CatMonitor.h"

#include "CatLink.h"

#include <QCheckBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>

namespace {

constexpr int kMaxLines = 5000;

// S-meter polls arrive several times a second and drown everything else.
bool isPolling(const QByteArray &frame)
{
    return frame.startsWith("SM0");
}

}   // namespace

CatMonitor::CatMonitor(ts590::CatLink *link, QWidget *parent)
    : QWidget(parent)
{
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(kMaxLines);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);

    m_hidePolling = new QCheckBox(tr("Hide S-meter polling"));
    m_hidePolling->setChecked(true);
    auto *clear = new QPushButton(tr("Clear"));
    connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);

    auto *controls = new QHBoxLayout;
    controls->addWidget(m_hidePolling);
    controls->addStretch();
    controls->addWidget(clear);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(controls);
    layout->addWidget(m_log);

    connect(link, &ts590::CatLink::frameSent, this,
            [this](const QByteArray &command) { append(QStringLiteral("→"), command.chopped(1)); });
    connect(link, &ts590::CatLink::frameReceived, this,
            [this](const QByteArray &frame) { append(QStringLiteral("←"), frame); });
    connect(link, &ts590::CatLink::commandRejected, this, [this](const QByteArray &command, char code) {
        append(QStringLiteral("✗"), QByteArray(1, code) + "  (" + command.chopped(command.isEmpty() ? 0 : 1) + ')');
    });
    connect(link, &ts590::CatLink::queryTimedOut, this,
            [this](const QByteArray &command) { append(QStringLiteral("⏱"), command.chopped(1)); });
}

void CatMonitor::append(const QString &direction, const QByteArray &frame)
{
    if (m_hidePolling->isChecked() && isPolling(frame))
        return;
    m_log->appendPlainText(QStringLiteral("%1 %2 %3;")
                               .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                    direction, QString::fromLatin1(frame)));
}
