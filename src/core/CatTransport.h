#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace ts590 {

// A byte pipe to the radio. Implementations: SerialTransport (the radio's
// USB virtual COM port) and SimulatorTransport (an in-process fake radio).
class CatTransport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual void write(const QByteArray &data) = 0;
    // Blocks briefly until queued output has left; used before closing.
    virtual void flush() {}
    virtual QString errorString() const = 0;
    virtual QString description() const = 0;
    // Wire time for `bytes`, so the command pacer doesn't outrun slow links.
    virtual int transmitTimeMs(qsizetype bytes) const
    {
        Q_UNUSED(bytes);
        return 0;
    }

signals:
    void dataReceived(const QByteArray &data);
    // The link died (e.g. USB cable pulled). The transport is closed already.
    void failed(const QString &message);
};

}   // namespace ts590
