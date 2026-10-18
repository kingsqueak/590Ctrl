#include "SerialTransport.h"

#include <QSerialPortInfo>

#include <algorithm>

namespace ts590 {

namespace {

// The TS-590SG's USB-B port exposes a Silicon Labs CP2102 next to its
// "USB Audio CODEC" interface.
constexpr quint16 kSiliconLabsVid = 0x10C4;
constexpr quint16 kCp210xPid = 0xEA60;

}   // namespace

SerialTransport::SerialTransport(const SerialSettings &settings, QObject *parent)
    : CatTransport(parent)
    , m_settings(settings)
{
    connect(&m_port, &QSerialPort::readyRead, this, [this] { emit dataReceived(m_port.readAll()); });
    connect(&m_port, &QSerialPort::errorOccurred, this, &SerialTransport::onError);
}

SerialTransport::~SerialTransport()
{
    close();
}

bool SerialTransport::open()
{
    m_port.setPortName(m_settings.portName);
    m_port.setBaudRate(m_settings.baudRate);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
    if (!m_port.open(QIODevice::ReadWrite))
        return false;

    // The radio only transmits while its CTS (our RTS) is asserted, so RTS must
    // be high. DTR stays low: it is commonly mapped to CW/FSK keying.
    m_port.setDataTerminalReady(false);
    m_port.setRequestToSend(true);
    m_port.clear();
    return true;
}

void SerialTransport::close()
{
    if (m_port.isOpen())
        m_port.close();
}

bool SerialTransport::isOpen() const
{
    return m_port.isOpen();
}

void SerialTransport::write(const QByteArray &data)
{
    if (m_port.isOpen())
        m_port.write(data);
}

void SerialTransport::flush()
{
    if (m_port.isOpen() && m_port.bytesToWrite() > 0)
        m_port.waitForBytesWritten(250);
}

QString SerialTransport::errorString() const
{
    return m_port.errorString();
}

QString SerialTransport::description() const
{
    return QStringLiteral("%1 @ %2 bps").arg(m_settings.portName).arg(m_settings.baudRate);
}

int SerialTransport::transmitTimeMs(qsizetype bytes) const
{
    // 8N1 = 10 bits per byte.
    return int((bytes * 10 * 1000 + m_settings.baudRate - 1) / m_settings.baudRate);
}

void SerialTransport::onError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_port.isOpen())
        return;
    // ResourceError is what an unplugged USB cable looks like.
    if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError) {
        const QString message = m_port.errorString();
        m_port.close();
        emit failed(message);
    }
}

QList<SerialPortCandidate> SerialTransport::availablePorts()
{
    QList<SerialPortCandidate> ports;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
#ifdef Q_OS_MACOS
        if (info.portName().startsWith(QLatin1String("tty.")))
            continue;
#endif
        SerialPortCandidate port;
        port.portName = info.portName();
        port.isTs590 = info.hasVendorIdentifier() && info.vendorIdentifier() == kSiliconLabsVid
                       && info.hasProductIdentifier() && info.productIdentifier() == kCp210xPid;
        port.description = info.description();
        ports.append(port);
    }
    // With the Silicon Labs VCP driver installed, macOS lists the same CP2102
    // twice: cu.SLAB_USBtoUART (VCP driver, works) and cu.usbserial-<serial>
    // (Apple's driver, silent while the VCP driver owns the device).
    auto rank = [](const SerialPortCandidate &port) {
        return (port.isTs590 ? 2 : 0) + (port.portName.contains(QLatin1String("SLAB_USBtoUART")) ? 1 : 0);
    };
    std::stable_sort(ports.begin(), ports.end(), [&](const auto &a, const auto &b) { return rank(a) > rank(b); });
    return ports;
}

}   // namespace ts590
