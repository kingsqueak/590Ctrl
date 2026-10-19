#pragma once

#include <QWidget>

class QCheckBox;
class QPlainTextEdit;

namespace ts590 {
class CatLink;
class TciServer;
}

// Raw CAT traffic log, for debugging against the real radio.
class CatMonitor : public QWidget
{
    Q_OBJECT

public:
    explicit CatMonitor(ts590::CatLink *link, QWidget *parent = nullptr);
    // Also logs the TCI server's clients and their commands.
    void addTciServer(ts590::TciServer *server);

private:
    void append(const QString &direction, const QByteArray &frame);
    void appendLine(const QString &text);

    QPlainTextEdit *m_log = nullptr;
    QCheckBox *m_hidePolling = nullptr;
};
