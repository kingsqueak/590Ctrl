#include "Qrz.h"

#include <QTest>
#include <QTimeZone>

using namespace qrz;

class TestQrz : public QObject
{
    Q_OBJECT

private slots:
    void parsesLookup()
    {
        // Shaped like the spec's example, plus an element QRZ might add later.
        const QByteArray xml = "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
            "<QRZDatabase version=\"1.34\" xmlns=\"http://xmldata.qrz.com\">\n"
            "  <Callsign>\n"
            "    <call>AA7BQ</call>\n"
            "    <fname>FRED L</fname>\n"
            "    <name>LLOYD</name>\n"
            "    <addr2>PHOENIX</addr2>\n"
            "    <state>AZ</state>\n"
            "    <county>Maricopa</county>\n"
            "    <land>United States</land>\n"
            "    <dxcc>291</dxcc>\n"
            "    <grid>DM32af</grid>\n"
            "    <cqzone>3</cqzone>\n"
            "    <ituzone>6</ituzone>\n"
            "    <lotw>1</lotw>\n"
            "    <newfield>something</newfield>\n"
            "  </Callsign>\n"
            "  <Session>\n"
            "    <Key>2331uf894c4bd29f3923f3bacf02c532d7bd9</Key>\n"
            "    <Count>123</Count>\n"
            "    <SubExp>Wed Jan 1 12:34:03 2025</SubExp>\n"
            "    <GMTime>Sun Aug 16 03:51:47 2024</GMTime>\n"
            "  </Session>\n"
            "</QRZDatabase>";
        const auto response = parseLookupResponse(xml);
        QVERIFY(response);
        QCOMPARE(response->key, QStringLiteral("2331uf894c4bd29f3923f3bacf02c532d7bd9"));
        QCOMPARE(response->lookupCount, QStringLiteral("123"));
        QCOMPARE(response->value(u"call"), QStringLiteral("AA7BQ"));
        QCOMPARE(response->value(u"grid"), QStringLiteral("DM32af"));
        QCOMPARE(response->value(u"newfield"), QStringLiteral("something"));
        QCOMPARE(response->callsign.size(), 13);
        QVERIFY(response->error.isEmpty());

        QCOMPARE(fieldLabel(QStringLiteral("fname")), QStringLiteral("First name"));
        QCOMPARE(fieldLabel(QStringLiteral("newfield")), QStringLiteral("newfield"));
        QVERIFY(fieldRank(QStringLiteral("call")) < fieldRank(QStringLiteral("grid")));
        QVERIFY(fieldRank(QStringLiteral("grid")) < fieldRank(QStringLiteral("newfield")));
        QCOMPARE(fieldDisplayValue(QStringLiteral("lotw"), QStringLiteral("1")), QStringLiteral("Yes"));
        QCOMPARE(fieldDisplayValue(QStringLiteral("grid"), QStringLiteral("1")), QStringLiteral("1"));
    }

    void parsesErrors()
    {
        const auto notFound = parseLookupResponse("<QRZDatabase version=\"1.34\">\n"
            "  <Session><Error>Not found: g1srdd</Error><Key>abc</Key></Session></QRZDatabase>");
        QVERIFY(notFound);
        QVERIFY(notFound->callsign.isEmpty());
        QCOMPARE(notFound->error, QStringLiteral("Not found: g1srdd"));
        QCOMPARE(notFound->key, QStringLiteral("abc"));

        // An expired session leaves out the key.
        const auto expired = parseLookupResponse(
            "<QRZDatabase><Session><Error>Session Timeout</Error></Session></QRZDatabase>");
        QVERIFY(expired);
        QVERIFY(expired->key.isEmpty());

        QVERIFY(!parseLookupResponse("<html>nope</html>"));
        QVERIFY(!parseLookupResponse(""));
    }

    void parsesLogbookAnswers()
    {
        const LogResult ok = parseLogResponse("RESULT=OK&LOGIDS=130877825&COUNT=1\n");
        QVERIFY(ok.ok());
        QCOMPARE(ok.logId, QStringLiteral("130877825"));
        const LogResult fail = parseLogResponse("RESULT=FAIL&REASON=Unable+to+add%3A+duplicate&COUNT=0");
        QVERIFY(!fail.ok());
        QCOMPARE(fail.reason, QStringLiteral("Unable to add: duplicate"));
        QCOMPARE(parseLogResponse("RESULT=AUTH").result, QStringLiteral("AUTH"));
    }

    void parsesAdif()
    {
        const auto records = parseAdif(u"header text <adif_ver:5>3.1.6 <eoh>\n"
                                       u"<CALL:4>W1AW <QSO_DATE:8:D>20250101<time_on:4>1200<mode:3>SSB <eor>\n"
                                       u"<call:4>W1AW<qso_date:8>20261018<time_on:6>221425<comment:9>a <b> & c<eor>");
        QCOMPARE(records.size(), 2);
        QCOMPARE(records[0].value("call"), QStringLiteral("W1AW"));
        QCOMPARE(records[0].value("qso_date"), QStringLiteral("20250101"));
        QCOMPARE(records[1].value("comment"), QStringLiteral("a <b> & c"));
        QCOMPARE(latestQso(records)->value("qso_date"), QStringLiteral("20261018"));
        QVERIFY(!latestQso({}));
    }

    void parsesFetchAnswers()
    {
        // HTML entities, as the logbook sends ADIF inside its name=value answer.
        const FetchResult fetch = parseFetchResponse(
            "RESULT=OK&COUNT=2&ADIF=&lt;call:4&gt;W1AW&lt;qso_date:8&gt;20250101&lt;time_on:4&gt;1200"
            "&lt;band:3&gt;20m&lt;mode:3&gt;SSB&lt;eor&gt;\n&lt;call:4&gt;W1AW&lt;qso_date:8&gt;20240101"
            "&lt;comment:5&gt;x &amp; y&lt;eor&gt;");
        QCOMPARE(fetch.result, QStringLiteral("OK"));
        QCOMPARE(fetch.count, 2);
        QCOMPARE(fetch.records.size(), 2);
        QCOMPARE(fetch.records[1].value("comment"), QStringLiteral("x & y"));
        QCOMPARE(latestQso(fetch.records)->value("band"), QStringLiteral("20m"));

        // Percent-encoded, with a field after the ADIF.
        const FetchResult encoded = parseFetchResponse("RESULT=OK&ADIF=%3Ccall%3A4%3EK1AB%3Ceor%3E&COUNT=1");
        QCOMPARE(encoded.records.size(), 1);
        QCOMPARE(encoded.count, 1);

        const FetchResult none = parseFetchResponse("RESULT=FAIL&REASON=no+log+entries+found&COUNT=0");
        QCOMPARE(none.result, QStringLiteral("FAIL"));
        QVERIFY(none.records.isEmpty());
    }

    void encodesForms()
    {
        QCOMPARE(formEncode({{"ADIF", QStringLiteral("<call:4>XX1X<eor>")}, {"P", QStringLiteral("a+b&c d")}}),
                 QByteArray("ADIF=%3Ccall%3A4%3EXX1X%3Ceor%3E&P=a%2Bb%26c%20d"));
    }

    void bandsAndModes()
    {
        QCOMPARE(adifBand(14'074'000), QStringLiteral("20m"));
        QCOMPARE(adifBand(3'856'000), QStringLiteral("80m"));
        QCOMPARE(adifBand(5'357'000), QStringLiteral("60m"));
        QCOMPARE(adifBand(50'313'000), QStringLiteral("6m"));
        QCOMPARE(adifBand(9'500'000), QString());
        QCOMPARE(adifModeFor(ts590::Mode::USB, false).submode, QStringLiteral("USB"));
        QCOMPARE(adifModeFor(ts590::Mode::USB, true).mode, QStringLiteral("PSK"));
        QCOMPARE(adifModeFor(ts590::Mode::LSB, true).submode, QStringLiteral("PSK31"));
        QCOMPARE(adifModeFor(ts590::Mode::FM, true).mode, QStringLiteral("PKT"));
        QVERIFY(!modeNames().contains(QStringLiteral("DATA")));
        for (const char *mode : {"PSK", "MFSK", "OLIVIA", "THOR", "HELL"}) {
            QVERIFY(modeNames().contains(QLatin1String(mode)));
            QCOMPARE(adifModeNamed(QLatin1String(mode), ts590::Mode::USB).mode, QLatin1String(mode));
            QVERIFY(adifModeNamed(QLatin1String(mode), ts590::Mode::USB).submode.isEmpty());
        }
        QCOMPARE(adifModeFor(ts590::Mode::CWR, false).mode, QStringLiteral("CW"));
        QCOMPARE(adifModeFor(ts590::Mode::FSK, false).mode, QStringLiteral("RTTY"));
        QCOMPARE(adifModeNamed(QStringLiteral("ssb"), ts590::Mode::LSB).submode, QStringLiteral("LSB"));
        QCOMPARE(adifModeNamed(QStringLiteral("FT4"), ts590::Mode::USB).mode, QStringLiteral("MFSK"));
        QCOMPARE(adifModeNamed(QStringLiteral("FT4"), ts590::Mode::USB).submode, QStringLiteral("FT4"));
        QCOMPARE(adifModeNamed(QStringLiteral("cw"), ts590::Mode::CW).mode, QStringLiteral("CW"));
        QCOMPARE(defaultReport(QStringLiteral("CW")), QStringLiteral("599"));
        QCOMPARE(defaultReport(QStringLiteral("SSB")), QStringLiteral("59"));
        QCOMPARE(defaultReport(QStringLiteral("FT8")), QString());
    }

    void buildsAdifRecord()
    {
        Qso qso;
        qso.call = QStringLiteral("aa7bq");
        qso.stationCall = QStringLiteral("KC2RGW");
        qso.timeOn = QDateTime(QDate(2026, 10, 18), QTime(23, 4, 5), QTimeZone::UTC);
        qso.txHz = 14'195'000;
        qso.mode = QStringLiteral("SSB");
        qso.submode = QStringLiteral("USB");
        qso.rstSent = QStringLiteral("59");
        qso.rstRcvd = QStringLiteral("57");
        qso.txPower = 100;
        QCOMPARE(adifRecord(qso),
                 QByteArray("<call:5>AA7BQ<qso_date:8>20261018<time_on:6>230405<band:3>20m<freq:9>14.195000"
                            "<mode:3>SSB<submode:3>USB<rst_sent:2>59<rst_rcvd:2>57<station_callsign:6>KC2RGW"
                            "<tx_pwr:3>100<eor>"));

        // Split adds the receive side; a lookup of the same call fills in the
        // station details.
        qso.rxHz = 14'200'000;
        LookupResponse lookup;
        lookup.callsign = {{"call", "AA7BQ"}, {"fname", "Fred"}, {"name", "Lloyd"}, {"state", "AZ"},
                           {"county", "Maricopa"}, {"grid", "DM32af"}, {"land", "United States"}, {"dxcc", "291"}};
        applyLookup(qso, lookup);
        const QByteArray record = adifRecord(qso);
        QVERIFY(record.contains("<freq_rx:9>14.200000"));
        QVERIFY(record.contains("<name:10>Fred Lloyd"));
        QVERIFY(record.contains("<cnty:11>AZ,Maricopa"));
        QVERIFY(record.contains("<gridsquare:6>DM32af"));
        QVERIFY(record.contains("<dxcc:3>291"));
        QVERIFY(record.endsWith("<eor>"));

        // A lookup of a different call is ignored.
        Qso other;
        other.call = QStringLiteral("W1AW");
        applyLookup(other, lookup);
        QVERIFY(other.name.isEmpty());

        // Lengths count bytes of UTF-8.
        other.comment = QStringLiteral("Zürich");
        QVERIFY(adifRecord(other).contains("<comment:7>Zürich"));
    }
};

QTEST_GUILESS_MAIN(TestQrz)
#include "tst_qrz.moc"
