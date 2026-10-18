#pragma once

#include "CatTransport.h"

#include <QList>
#include <QSerialPort>

namespace ts590 {

struct SerialSettings {
    QString portName;
    qint32 baudRate = 115200;   // menu EX068 (USB port speed); factory default
};

struct SerialPortCandidate {
    QString portName;
    QString description;
    bool isTs590 = false;   // matches the radio's built-in CP210x USB bridge
};

class SerialTransport : public CatTransport
{
    Q_OBJECT

public:
    explicit SerialTransport(const SerialSettings &settings, QObject *parent = nullptr);
    ~SerialTransport() override;

    bool open() override;
    void close() override;
    bool isOpen() const override;
    void write(const QByteArray &data) override;
    void flush() override;
    QString errorString() const override;
    QString description() const override;
    int transmitTimeMs(qsizetype bytes) const override;

    // Serial ports worth offering, radio candidates first. On macOS only the
    // callout (cu.*) devices are listed; the tty.* twins block on open.
    static QList<SerialPortCandidate> availablePorts();

private:
    void onError(QSerialPort::SerialPortError error);

    SerialSettings m_settings;
    QSerialPort m_port;
};

}   // namespace ts590
