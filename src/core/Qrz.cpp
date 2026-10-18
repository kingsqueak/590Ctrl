#include "Qrz.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <algorithm>

namespace qrz {

namespace {

constexpr int kTimeoutMs = 15000;

const QUrl kXmlUrl(QStringLiteral("https://xmldata.qrz.com/xml/current/"));
const QUrl kLogbookUrl(QStringLiteral("https://logbook.qrz.com/api"));

// Callsign fields in the spec's order, with display names.
const QList<std::pair<QString, QString>> &knownFields()
{
    static const QList<std::pair<QString, QString>> fields = {
        {QStringLiteral("call"), QStringLiteral("Callsign")},
        {QStringLiteral("xref"), QStringLiteral("Looked up as")},
        {QStringLiteral("aliases"), QStringLiteral("Aliases")},
        {QStringLiteral("p_call"), QStringLiteral("Previous call")},
        {QStringLiteral("name_fmt"), QStringLiteral("Name")},
        {QStringLiteral("fname"), QStringLiteral("First name")},
        {QStringLiteral("name"), QStringLiteral("Last name")},
        {QStringLiteral("nickname"), QStringLiteral("Nickname")},
        {QStringLiteral("born"), QStringLiteral("Born")},
        {QStringLiteral("attn"), QStringLiteral("Attention")},
        {QStringLiteral("addr1"), QStringLiteral("Address")},
        {QStringLiteral("addr2"), QStringLiteral("City")},
        {QStringLiteral("state"), QStringLiteral("State")},
        {QStringLiteral("zip"), QStringLiteral("Postal code")},
        {QStringLiteral("country"), QStringLiteral("Country")},
        {QStringLiteral("ccode"), QStringLiteral("Country code")},
        {QStringLiteral("county"), QStringLiteral("County")},
        {QStringLiteral("fips"), QStringLiteral("FIPS")},
        {QStringLiteral("land"), QStringLiteral("DXCC entity")},
        {QStringLiteral("dxcc"), QStringLiteral("DXCC")},
        {QStringLiteral("grid"), QStringLiteral("Grid")},
        {QStringLiteral("lat"), QStringLiteral("Latitude")},
        {QStringLiteral("lon"), QStringLiteral("Longitude")},
        {QStringLiteral("geoloc"), QStringLiteral("Location source")},
        {QStringLiteral("cqzone"), QStringLiteral("CQ zone")},
        {QStringLiteral("ituzone"), QStringLiteral("ITU zone")},
        {QStringLiteral("iota"), QStringLiteral("IOTA")},
        {QStringLiteral("TimeZone"), QStringLiteral("Time zone")},
        {QStringLiteral("GMTOffset"), QStringLiteral("GMT offset")},
        {QStringLiteral("DST"), QStringLiteral("Observes DST")},
        {QStringLiteral("MSA"), QStringLiteral("Metro area")},
        {QStringLiteral("AreaCode"), QStringLiteral("Area code")},
        {QStringLiteral("class"), QStringLiteral("License class")},
        {QStringLiteral("codes"), QStringLiteral("License codes")},
        {QStringLiteral("efdate"), QStringLiteral("License effective")},
        {QStringLiteral("expdate"), QStringLiteral("License expires")},
        {QStringLiteral("email"), QStringLiteral("Email")},
        {QStringLiteral("url"), QStringLiteral("Web page")},
        {QStringLiteral("qslmgr"), QStringLiteral("QSL manager")},
        {QStringLiteral("lotw"), QStringLiteral("LoTW")},
        {QStringLiteral("eqsl"), QStringLiteral("eQSL")},
        {QStringLiteral("mqsl"), QStringLiteral("Paper QSL")},
        {QStringLiteral("bio"), QStringLiteral("Bio size")},
        {QStringLiteral("biodate"), QStringLiteral("Bio updated")},
        {QStringLiteral("image"), QStringLiteral("Image")},
        {QStringLiteral("imageinfo"), QStringLiteral("Image info")},
        {QStringLiteral("u_views"), QStringLiteral("Page views")},
        {QStringLiteral("user"), QStringLiteral("Managed by")},
        {QStringLiteral("serial"), QStringLiteral("QRZ serial")},
        {QStringLiteral("moddate"), QStringLiteral("Last modified")},
    };
    return fields;
}

void appendAdif(QByteArray &out, const char *name, const QString &value)
{
    const QByteArray data = value.trimmed().toUtf8();
    if (data.isEmpty())
        return;
    out += '<' + QByteArray(name) + ':' + QByteArray::number(data.size()) + '>' + data;
}

}   // namespace

// ------------------------------------------------------------------ Lookups

QString LookupResponse::value(QStringView name) const
{
    for (const Field &field : callsign) {
        if (field.name == name)
            return field.value;
    }
    return {};
}

std::optional<LookupResponse> parseLookupResponse(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    if (!reader.readNextStartElement() || reader.name() != QLatin1String("QRZDatabase"))
        return std::nullopt;
    LookupResponse response;
    // Elements are matched by name; anything unknown is skipped.
    while (reader.readNextStartElement()) {
        const QString section = reader.name().toString();
        if (section != QLatin1String("Callsign") && section != QLatin1String("Session")) {
            reader.skipCurrentElement();
            continue;
        }
        while (reader.readNextStartElement()) {
            const QString name = reader.name().toString();
            const QString text = reader.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            if (section == QLatin1String("Callsign")) {
                response.callsign.append({name, text});
            } else if (name == QLatin1String("Key")) {
                response.key = text;
            } else if (name == QLatin1String("Error")) {
                response.error = text;
            } else if (name == QLatin1String("Message")) {
                response.message = text;
            } else if (name == QLatin1String("SubExp")) {
                response.subscriptionExpiry = text;
            } else if (name == QLatin1String("Count")) {
                response.lookupCount = text;
            }
        }
    }
    if (reader.hasError() && reader.error() != QXmlStreamReader::PrematureEndOfDocumentError)
        return std::nullopt;
    return response;
}

QString fieldLabel(const QString &name)
{
    for (const auto &[field, label] : knownFields()) {
        if (field == name)
            return label;
    }
    return name;
}

int fieldRank(const QString &name)
{
    const auto &fields = knownFields();
    for (qsizetype i = 0; i < fields.size(); ++i) {
        if (fields[i].first == name)
            return int(i);
    }
    return int(fields.size());
}

QString fieldDisplayValue(const QString &name, const QString &value)
{
    if (name == QLatin1String("eqsl") || name == QLatin1String("mqsl") || name == QLatin1String("lotw")
        || name == QLatin1String("DST")) {
        if (value == QLatin1String("1") || value == QLatin1String("Y"))
            return QStringLiteral("Yes");
        if (value == QLatin1String("0") || value == QLatin1String("N"))
            return QStringLiteral("No");
    }
    return value;
}

// ---------------------------------------------------------------- Logbook

LogResult parseLogResponse(const QByteArray &body)
{
    LogResult result;
    for (const QByteArray &pair : body.trimmed().split('&')) {
        const qsizetype equals = pair.indexOf('=');
        if (equals < 0)
            continue;
        const QByteArray name = pair.left(equals).trimmed().toUpper();
        QByteArray raw = pair.mid(equals + 1);
        raw.replace('+', ' ');
        const QString value = QUrl::fromPercentEncoding(raw).trimmed();
        if (name == "RESULT")
            result.result = value.toUpper();
        else if (name == "LOGID" || name == "LOGIDS")
            result.logId = value;
        else if (name == "REASON")
            result.reason = value;
    }
    return result;
}

QList<AdifFields> parseAdif(QStringView adi)
{
    QList<AdifFields> records;
    AdifFields record;
    qsizetype pos = 0;
    while ((pos = adi.indexOf(QLatin1Char('<'), pos)) >= 0) {
        const qsizetype close = adi.indexOf(QLatin1Char('>'), pos);
        if (close < 0)
            break;
        // <name:length[:type]>, or <eor> / <eoh> without a length.
        const QList<QStringView> parts = adi.sliced(pos + 1, close - pos - 1).split(QLatin1Char(':'));
        const QString name = parts[0].trimmed().toString().toLower();
        pos = close + 1;
        if (name == QLatin1String("eor")) {
            if (!record.isEmpty())
                records << record;
            record.clear();
        } else if (name == QLatin1String("eoh")) {
            record.clear();
        } else if (parts.size() >= 2) {
            bool ok = false;
            const qsizetype length = parts[1].toLongLong(&ok);
            if (!ok || length < 0)
                continue;
            record.insert(name, adi.sliced(pos, std::min(length, adi.size() - pos)).toString());
            pos += length;
        }
    }
    return records;
}

FetchResult parseFetchResponse(const QByteArray &body)
{
    FetchResult fetch;
    // ADIF holds '&' and '=' of its own, so it's cut out before the rest is
    // split into name=value pairs. It's normally the last field.
    QByteArray head = body.trimmed();
    QByteArray adif;
    if (const qsizetype at = head.indexOf("ADIF="); at >= 0) {
        adif = head.mid(at + 5);
        head.truncate(at);
        // Anything after the last end-of-record is a field that followed it.
        for (const QByteArray eor : {QByteArray("<eor>"), QByteArray("&lt;eor&gt;"), QByteArray("%3Ceor%3E")}) {
            if (const qsizetype end = adif.toLower().lastIndexOf(eor.toLower()); end >= 0) {
                head += '&' + adif.mid(end + eor.size());
                adif.truncate(end + eor.size());
                break;
            }
        }
    }
    const LogResult status = parseLogResponse(head);
    fetch.result = status.result;
    fetch.reason = status.reason;
    for (const QByteArray &pair : head.split('&')) {
        if (pair.trimmed().toUpper().startsWith("COUNT="))
            fetch.count = pair.trimmed().mid(6).toInt();
    }
    if (adif.contains("%3C") || adif.contains("%3c"))
        adif = QByteArray::fromPercentEncoding(adif);
    QString text = QString::fromUtf8(adif);
    if (text.contains(QLatin1String("&lt;"))) {
        text.replace(QLatin1String("&lt;"), QLatin1String("<"));
        text.replace(QLatin1String("&gt;"), QLatin1String(">"));
        text.replace(QLatin1String("&quot;"), QLatin1String("\""));
        text.replace(QLatin1String("&#39;"), QLatin1String("'"));
        text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    }
    fetch.records = parseAdif(text);
    return fetch;
}

std::optional<AdifFields> latestQso(const QList<AdifFields> &records)
{
    std::optional<AdifFields> latest;
    auto stamp = [](const AdifFields &qso) {
        return qso.value(QStringLiteral("qso_date")) + qso.value(QStringLiteral("time_on")).leftJustified(6, u'0');
    };
    for (const AdifFields &qso : records) {
        if (!latest || stamp(qso) > stamp(*latest))
            latest = qso;
    }
    return latest;
}

QByteArray formEncode(const QList<std::pair<QByteArray, QString>> &fields)
{
    QByteArray out;
    for (const auto &[name, value] : fields) {
        if (!out.isEmpty())
            out += '&';
        out += name + '=' + value.toUtf8().toPercentEncoding();
    }
    return out;
}

QString adifBand(qint64 hz)
{
    struct Band {
        qint64 low, high;
        const char *name;
    };
    static constexpr Band bands[] = {
        {1'800'000, 2'000'000, "160m"},    {3'500'000, 4'000'000, "80m"},   {5'060'000, 5'450'000, "60m"},
        {7'000'000, 7'300'000, "40m"},     {10'100'000, 10'150'000, "30m"}, {14'000'000, 14'350'000, "20m"},
        {18'068'000, 18'168'000, "17m"},   {21'000'000, 21'450'000, "15m"}, {24'890'000, 24'990'000, "12m"},
        {28'000'000, 29'700'000, "10m"},   {50'000'000, 54'000'000, "6m"},  {472'000, 479'000, "630m"},
        {135'700, 137'800, "2190m"},
    };
    for (const Band &band : bands) {
        if (hz >= band.low && hz <= band.high)
            return QString::fromLatin1(band.name);
    }
    return {};
}

AdifMode adifModeFor(ts590::Mode mode, bool data)
{
    using ts590::Mode;
    switch (mode) {
    case Mode::LSB:
    case Mode::USB:
        if (data)
            return {QStringLiteral("PSK"), QStringLiteral("PSK31")};
        return {QStringLiteral("SSB"), mode == Mode::LSB ? QStringLiteral("LSB") : QStringLiteral("USB")};
    case Mode::CW:
    case Mode::CWR: return {QStringLiteral("CW"), {}};
    case Mode::FSK:
    case Mode::FSKR: return {QStringLiteral("RTTY"), {}};
    case Mode::FM: return {data ? QStringLiteral("PKT") : QStringLiteral("FM"), {}};
    case Mode::AM: return {data ? QStringLiteral("PKT") : QStringLiteral("AM"), {}};
    case Mode::None: break;
    }
    return {};
}

QStringList modeNames()
{
    // Plain ADIF modes, plus FT4 and PSK31 for their submodes.
    return {QStringLiteral("SSB"),   QStringLiteral("CW"),   QStringLiteral("AM"),     QStringLiteral("FM"),
            QStringLiteral("RTTY"),  QStringLiteral("FT8"),  QStringLiteral("FT4"),    QStringLiteral("PSK31"),
            QStringLiteral("PSK"),   QStringLiteral("MFSK"), QStringLiteral("OLIVIA"), QStringLiteral("THOR"),
            QStringLiteral("HELL"),  QStringLiteral("PKT")};
}

AdifMode adifModeNamed(const QString &name, ts590::Mode radioMode)
{
    const QString upper = name.trimmed().toUpper();
    if (upper == QLatin1String("SSB")) {
        const QString sideband = radioMode == ts590::Mode::LSB   ? QStringLiteral("LSB")
                                 : radioMode == ts590::Mode::USB ? QStringLiteral("USB")
                                                                 : QString();
        return {upper, sideband};
    }
    if (upper == QLatin1String("FT4"))
        return {QStringLiteral("MFSK"), upper};
    if (upper == QLatin1String("PSK31"))
        return {QStringLiteral("PSK"), upper};
    return {upper, {}};
}

QString defaultReport(const QString &adifMode)
{
    if (adifMode == QLatin1String("CW") || adifMode == QLatin1String("RTTY"))
        return QStringLiteral("599");
    if (adifMode == QLatin1String("SSB") || adifMode == QLatin1String("FM") || adifMode == QLatin1String("AM"))
        return QStringLiteral("59");
    return {};
}

void applyLookup(Qso &qso, const LookupResponse &lookup)
{
    const QString call = lookup.value(u"call");
    const QString xref = lookup.value(u"xref");
    if (call.compare(qso.call, Qt::CaseInsensitive) != 0 && xref.compare(qso.call, Qt::CaseInsensitive) != 0)
        return;
    qso.name = (lookup.value(u"fname") + QLatin1Char(' ') + lookup.value(u"name")).trimmed();
    qso.qth = lookup.value(u"addr2");
    qso.grid = lookup.value(u"grid");
    qso.country = lookup.value(u"land");
    qso.state = lookup.value(u"state");
    qso.county = lookup.value(u"county");
    qso.dxcc = lookup.value(u"dxcc");
    qso.cqZone = lookup.value(u"cqzone");
    qso.ituZone = lookup.value(u"ituzone");
}

QByteArray adifRecord(const Qso &qso)
{
    const QDateTime utc = qso.timeOn.toUTC();
    QByteArray out;
    appendAdif(out, "call", qso.call.toUpper());
    appendAdif(out, "qso_date", utc.toString(QStringLiteral("yyyyMMdd")));
    appendAdif(out, "time_on", utc.toString(QStringLiteral("HHmmss")));
    appendAdif(out, "band", adifBand(qso.txHz));
    appendAdif(out, "freq", QString::number(qso.txHz / 1e6, 'f', 6));
    if (qso.rxHz > 0 && qso.rxHz != qso.txHz) {
        appendAdif(out, "band_rx", adifBand(qso.rxHz));
        appendAdif(out, "freq_rx", QString::number(qso.rxHz / 1e6, 'f', 6));
    }
    appendAdif(out, "mode", qso.mode.toUpper());
    appendAdif(out, "submode", qso.submode.toUpper());
    appendAdif(out, "rst_sent", qso.rstSent);
    appendAdif(out, "rst_rcvd", qso.rstRcvd);
    appendAdif(out, "station_callsign", qso.stationCall.toUpper());
    if (qso.txPower)
        appendAdif(out, "tx_pwr", QString::number(*qso.txPower));
    appendAdif(out, "name", qso.name);
    appendAdif(out, "qth", qso.qth);
    appendAdif(out, "gridsquare", qso.grid);
    appendAdif(out, "country", qso.country);
    appendAdif(out, "dxcc", qso.dxcc);
    appendAdif(out, "state", qso.state);
    // ADIF wants the county as "ST,County".
    if (!qso.state.isEmpty() && !qso.county.isEmpty())
        appendAdif(out, "cnty", qso.state + QLatin1Char(',') + qso.county);
    appendAdif(out, "cqz", qso.cqZone);
    appendAdif(out, "ituz", qso.ituZone);
    appendAdif(out, "comment", qso.comment);
    return out + "<eor>";
}

// ---------------------------------------------------------------- Client

Client::Client(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_xmlUrl(kXmlUrl)
    , m_logbookUrl(kLogbookUrl)
{
}

void Client::setCredentials(const QString &username, const QString &password)
{
    if (username.trimmed() == m_username && password == m_password)
        return;
    m_username = username.trimmed();
    m_password = password;
    m_sessionKey.clear();
}

QNetworkReply *Client::post(const QUrl &url, const QByteArray &body)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, m_agent);
    request.setTransferTimeout(kTimeoutMs);
    return m_network->post(request, body);
}

void Client::lookup(const QString &callsign)
{
    m_callsign = callsign.trimmed().toUpper();
    ++m_generation;
    if (!hasCredentials()) {
        emit lookupFailed(m_callsign, tr("Enter your QRZ username and password under Account."));
        return;
    }
    if (m_sessionKey.isEmpty())
        login();
    else
        requestLookup(true);
}

void Client::login()
{
    // POST keeps the password out of the URL.
    QNetworkReply *reply = post(m_xmlUrl, formEncode({{"username", m_username},
                                                      {"password", m_password},
                                                      {"agent", m_agent}}));
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] { handleXml(reply, generation, true, false); });
}

void Client::requestLookup(bool mayRelogin)
{
    QUrl url = m_xmlUrl;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("s"), m_sessionKey);
    query.addQueryItem(QStringLiteral("callsign"), m_callsign);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, m_agent);
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, mayRelogin] { handleXml(reply, generation, false, mayRelogin); });
}

void Client::handleXml(QNetworkReply *reply, int generation, bool isLogin, bool mayRelogin)
{
    reply->deleteLater();
    if (generation != m_generation)
        return;   // superseded by a newer lookup
    if (reply->error() != QNetworkReply::NoError) {
        emit lookupFailed(m_callsign, tr("Could not reach QRZ: %1").arg(reply->errorString()));
        return;
    }
    const auto response = parseLookupResponse(reply->readAll());
    if (!response) {
        emit lookupFailed(m_callsign, tr("QRZ sent an answer that isn't valid XML."));
        return;
    }
    if (response->key.isEmpty()) {
        // No key: the login failed, or the session expired (QRZ drops keys
        // whenever it likes, e.g. when the IP address changes).
        m_sessionKey.clear();
        if (!isLogin && mayRelogin) {
            login();
            return;
        }
        emit lookupFailed(m_callsign, response->error.isEmpty() ? tr("QRZ login failed.") : response->error);
        return;
    }
    m_sessionKey = response->key;
    if (isLogin) {
        requestLookup(false);
        return;
    }
    if (response->callsign.isEmpty()) {
        emit lookupFailed(m_callsign, response->error.isEmpty() ? tr("%1 was not found.").arg(m_callsign)
                                                                : response->error);
        return;
    }
    emit lookupFinished(*response);
}

void Client::insertQso(const QByteArray &adif)
{
    if (!hasLogbookKey()) {
        emit qsoFailed(tr("Enter your QRZ Logbook API key under Account."));
        return;
    }
    QNetworkReply *reply = post(m_logbookUrl, formEncode({{"KEY", m_logbookKey},
                                                          {"ACTION", QStringLiteral("INSERT")},
                                                          {"ADIF", QString::fromUtf8(adif)}}));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit qsoFailed(tr("Could not reach the QRZ Logbook: %1").arg(reply->errorString()));
            return;
        }
        const LogResult result = parseLogResponse(reply->readAll());
        if (result.ok())
            emit qsoInserted(result);
        else if (result.result == QLatin1String("AUTH"))
            emit qsoFailed(tr("The QRZ Logbook refused the API key (it needs a QRZ subscription and the "
                              "key of the logbook for this station callsign)."));
        else
            emit qsoFailed(result.reason.isEmpty() ? tr("The QRZ Logbook did not accept the QSO.") : result.reason);
    });
}

void Client::checkLog(const QString &callsign)
{
    const QString call = callsign.trimmed().toUpper();
    if (!hasLogbookKey() || call.isEmpty())
        return;
    QNetworkReply *reply = post(m_logbookUrl, formEncode({{"KEY", m_logbookKey},
                                                          {"ACTION", QStringLiteral("FETCH")},
                                                          {"OPTION", QStringLiteral("CALL:") + call}}));
    connect(reply, &QNetworkReply::finished, this, [this, reply, call] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit logCheckFailed(call, tr("Could not reach the QRZ Logbook: %1").arg(reply->errorString()));
            return;
        }
        const FetchResult result = parseFetchResponse(reply->readAll());
        if (result.result == QLatin1String("AUTH")) {
            emit logCheckFailed(call, tr("The QRZ Logbook refused the API key."));
            return;
        }
        // FAIL with nothing found just means not worked; a bad key or
        // request says so in REASON.
        const QString reason = result.reason.toLower();
        if (result.result == QLatin1String("FAIL") && result.records.isEmpty()
            && (reason.contains(QLatin1String("key")) || reason.contains(QLatin1String("invalid"))
                || reason.contains(QLatin1String("auth")))) {
            emit logCheckFailed(call, result.reason);
            return;
        }
        emit logChecked(call, result);
    });
}

void Client::fetchImage(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, m_agent);
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            emit imageFetched(url, reply->readAll());
    });
}

}   // namespace qrz
