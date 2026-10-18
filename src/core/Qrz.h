#pragma once

// QRZ.com: callsign lookups through the XML data service and QSO uploads
// through the Logbook API.
//
// References: "QRZ XML Interface Specification" 1.34
// (https://www.qrz.com/docs/xml/current_spec.html) and the QRZ Logbook API
// developer guide (https://www.qrz.com/docs/logbook/QRZLogbookAPI.html).

#include "CatCodec.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>
#include <utility>

class QNetworkAccessManager;
class QNetworkReply;

namespace qrz {

// ------------------------------------------------------------------ Lookups

struct Field {
    QString name;    // element name as QRZ sends it, e.g. "fname", "GMTOffset"
    QString value;
};

// One XML answer. Login answers carry only the session part.
struct LookupResponse {
    QList<Field> callsign;   // <Callsign> children in document order; empty if none
    QString key;             // session key; empty when the session is gone
    QString error;
    QString message;         // informational, e.g. a subscription notice
    QString subscriptionExpiry;
    QString lookupCount;     // lookups in the last 24 hours

    QString value(QStringView name) const;   // "" if absent
};
// nullopt if the body isn't a QRZDatabase document.
std::optional<LookupResponse> parseLookupResponse(const QByteArray &xml);

// Readable name for a Callsign field ("fname" -> "First name"); unknown
// fields come back unchanged.
QString fieldLabel(const QString &name);
// The order the spec lists the fields in, for display; unknown fields sort
// after these in the order received.
int fieldRank(const QString &name);
// eqsl/mqsl/lotw "1"/"0" as Yes/No; everything else unchanged.
QString fieldDisplayValue(const QString &name, const QString &value);

// ---------------------------------------------------------------- Logbook

struct LogResult {
    QString result;   // OK, REPLACE, FAIL or AUTH
    QString logId;    // LOGID (the guide's example says LOGIDS)
    QString reason;   // with FAIL
    bool ok() const { return result == QLatin1String("OK") || result == QLatin1String("REPLACE"); }
};
LogResult parseLogResponse(const QByteArray &body);

// One ADIF record: lower-case field name -> value.
using AdifFields = QHash<QString, QString>;
// Reads ADI records (anything before the first tag, e.g. a header, is
// skipped). Field lengths are taken as characters.
QList<AdifFields> parseAdif(QStringView adi);

// Answer to ACTION=FETCH.
struct FetchResult {
    QString result;   // OK, FAIL or AUTH
    QString reason;
    int count = 0;    // matching QSOs, regardless of MAX
    QList<AdifFields> records;
};
// The ADIF part may come HTML-entity or percent encoded; both are undone.
FetchResult parseFetchResponse(const QByteArray &body);
// The most recent record by qso_date and time_on, or nullopt.
std::optional<AdifFields> latestQso(const QList<AdifFields> &records);

// application/x-www-form-urlencoded, encoding everything outside
// [A-Za-z0-9-._~] (including '+', '&', '<', '>' and ':').
QByteArray formEncode(const QList<std::pair<QByteArray, QString>> &fields);

// ADIF band for a frequency ("20m"), or "" outside the amateur bands.
QString adifBand(qint64 hz);

struct AdifMode {
    QString mode;      // "SSB", "CW", "RTTY", "FM", "AM", "FT8", "PSK", "PKT", ...
    QString submode;   // "USB"/"LSB" for SSB, "PSK31" for PSK
};
// Best guess from the radio. The digital mode behind DATA can't be known:
// USB/LSB-DATA is assumed PSK31 and FM/AM-DATA packet, and the UI lets the
// user change it.
AdifMode adifModeFor(ts590::Mode mode, bool data);
// The names offered in the UI's mode list.
QStringList modeNames();
// ADIF mode/submode for a name from modeNames() or typed by the user. SSB
// takes its sideband from the radio; FT4 and PSK31 are submodes in ADIF.
AdifMode adifModeNamed(const QString &name, ts590::Mode radioMode);
// The usual signal report to prefill: "599" for CW/RTTY, "59" for phone,
// "" for digital modes that send dB reports.
QString defaultReport(const QString &adifMode);

struct Qso {
    QString call;
    QString stationCall;
    QDateTime timeOn;   // UTC
    qint64 txHz = 0;
    qint64 rxHz = 0;    // set when split; 0 otherwise
    QString mode;
    QString submode;
    QString rstSent;
    QString rstRcvd;
    std::optional<int> txPower;
    QString comment;
    // From a lookup of the same call, when there is one.
    QString name;
    QString qth;
    QString grid;
    QString country;   // DXCC entity name
    QString state;
    QString county;
    QString dxcc;
    QString cqZone;
    QString ituZone;
};
// Copies the lookup's details into the QSO (only if it's the same call).
void applyLookup(Qso &qso, const LookupResponse &lookup);
// One ADI record ending in <eor>; empty fields are left out.
QByteArray adifRecord(const Qso &qso);

// ---------------------------------------------------------------- Client

// Holds the QRZ session and talks to both services. Logs in on the first
// lookup and again whenever QRZ drops the session.
class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);

    // Changing the account drops the session.
    void setCredentials(const QString &username, const QString &password);
    void setLogbookKey(const QString &key) { m_logbookKey = key.trimmed(); }
    // Sent as the XML "agent" and as the User-Agent, e.g. "590Ctrl/0.11".
    void setAgent(const QString &agent) { m_agent = agent; }
    QString password() const { return m_password; }
    bool hasCredentials() const { return !m_username.isEmpty() && !m_password.isEmpty(); }
    bool hasLogbookKey() const { return !m_logbookKey.isEmpty(); }

    // A newer lookup supersedes one still in flight.
    void lookup(const QString &callsign);
    void insertQso(const QByteArray &adif);
    // Fetches the logbook's QSOs with this call (needs the API key).
    void checkLog(const QString &callsign);
    void fetchImage(const QUrl &url);

    // For tests.
    void setXmlUrl(const QUrl &url) { m_xmlUrl = url; }
    void setLogbookUrl(const QUrl &url) { m_logbookUrl = url; }

signals:
    void lookupFinished(const qrz::LookupResponse &response);
    void lookupFailed(const QString &callsign, const QString &error);
    void qsoInserted(const qrz::LogResult &result);
    void qsoFailed(const QString &reason);
    void logChecked(const QString &callsign, const qrz::FetchResult &result);
    void logCheckFailed(const QString &callsign, const QString &error);
    void imageFetched(const QUrl &url, const QByteArray &data);

private:
    void login();
    void requestLookup(bool mayRelogin);
    void handleXml(QNetworkReply *reply, int generation, bool isLogin, bool mayRelogin);
    QNetworkReply *post(const QUrl &url, const QByteArray &body);

    QNetworkAccessManager *m_network;
    QUrl m_xmlUrl;
    QUrl m_logbookUrl;
    QString m_agent;
    QString m_username;
    QString m_password;
    QString m_logbookKey;
    QString m_sessionKey;
    QString m_callsign;     // the lookup in progress
    int m_generation = 0;   // bumps with each lookup; stale replies are dropped
};

}   // namespace qrz
