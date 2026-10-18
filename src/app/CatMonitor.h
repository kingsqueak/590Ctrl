#pragma once

#include <QWidget>

class QCheckBox;
class QPlainTextEdit;

namespace ts590 {
class CatLink;
}

// Raw CAT traffic log, for debugging against the real radio.
class CatMonitor : public QWidget
{
    Q_OBJECT

public:
    explicit CatMonitor(ts590::CatLink *link, QWidget *parent = nullptr);

private:
    void append(const QString &direction, const QByteArray &frame);

    QPlainTextEdit *m_log = nullptr;
    QCheckBox *m_hidePolling = nullptr;
};
