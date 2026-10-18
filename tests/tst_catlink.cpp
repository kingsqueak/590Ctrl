#include "CatLink.h"
#include "CatTransport.h"

#include <QSignalSpy>
#include <QTest>

using namespace ts590;

namespace {

// Records writes; the test injects answers by hand.
class FakeTransport : public CatTransport
{
public:
    bool open() override { return m_open = true; }
    void close() override { m_open = false; }
    bool isOpen() const override { return m_open; }
    void write(const QByteArray &data) override { written.append(data); }
    QString errorString() const override { return {}; }
    QString description() const override { return QStringLiteral("fake"); }
    void inject(const QByteArray &bytes) { emit dataReceived(bytes); }

    QByteArrayList written;

private:
    bool m_open = false;
};

}   // namespace

class TestCatLink : public QObject
{
    Q_OBJECT

private:
    FakeTransport *attach(CatLink &link)
    {
        auto transport = std::make_unique<FakeTransport>();
        FakeTransport *raw = transport.get();
        raw->open();
        link.setTransport(std::move(transport));
        return raw;
    }

private slots:
    void framesSplitAcrossReads()
    {
        CatLink link;
        FakeTransport *t = attach(link);
        QSignalSpy frames(&link, &CatLink::frameReceived);
        t->inject("FA000141");
        t->inject("95000;AG0");
        t->inject("100;");
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames[0][0].toByteArray(), QByteArray("FA00014195000"));
        QCOMPARE(frames[1][0].toByteArray(), QByteArray("AG0100"));
    }

    void queryWaitsForAnswerBeforeNextCommand()
    {
        CatLink link;
        FakeTransport *t = attach(link);
        link.query("FA;");
        link.send("AG0100;");
        QCOMPARE(t->written, QByteArrayList({"FA;"}));
        t->inject("FA00014195000;");
        QCOMPARE(t->written, QByteArrayList({"FA;", "AG0100;"}));
    }

    void errorIsAttributedToInFlightCommand()
    {
        CatLink link;
        FakeTransport *t = attach(link);
        QSignalSpy rejected(&link, &CatLink::commandRejected);
        link.query("FW;");
        t->inject("?;");
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(rejected[0][0].toByteArray(), QByteArray("FW;"));
        QCOMPARE(rejected[0][1].value<char>(), '?');
        QCOMPARE(link.pendingCount(), 0);
    }

    void queuedSetsWithSameKeyCoalesce()
    {
        CatLink link;
        FakeTransport *t = attach(link);
        link.query("FA;");   // in flight; holds the queue
        link.send("AG0010;", "AG");
        link.send("AG0020;", "AG");
        link.query("ID;");
        link.send("SQ0005;", "SQ");
        link.send("AG0030;", "AG");
        link.query("ID;");   // duplicate of a queued query is dropped
        QCOMPARE(link.pendingCount(), 4);
        t->inject("FA00014195000;");
        QTRY_COMPARE(t->written, QByteArrayList({"FA;", "AG0030;", "ID;"}));
        t->inject("ID023;");
        QTRY_COMPARE(t->written, QByteArrayList({"FA;", "AG0030;", "ID;", "SQ0005;"}));
    }

    void queryTimesOut()
    {
        CatLink link;
        link.setReplyTimeout(20);
        FakeTransport *t = attach(link);
        QSignalSpy timedOut(&link, &CatLink::queryTimedOut);
        link.query("ID;");
        link.query("FV;");
        QTRY_COMPARE(timedOut.size(), 2);
        QCOMPARE(t->written, QByteArrayList({"ID;", "FV;"}));
    }

    void skipsLeadingJunk()
    {
        CatLink link;
        FakeTransport *t = attach(link);
        QSignalSpy frames(&link, &CatLink::frameReceived);
        t->inject(QByteArray("\xfe\x00\r\nID023;", 10));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0][0].toByteArray(), QByteArray("ID023"));
    }
};

QTEST_GUILESS_MAIN(TestCatLink)
#include "tst_catlink.moc"
