#include "CwWordFormer.h"

#include <QTest>

using namespace ts590;

class TestCwWordFormer : public QObject
{
    Q_OBJECT

private slots:
    void forms_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");

        QTest::newRow("empty") << "" << "";
        QTest::newRow("already spaced") << "CQ CQ DE K1XM K " << "CQ CQ DE K1XM K ";
        // The call has been seen on its own, which settles where it starts.
        QTest::newRow("contest run") << "K1XM CQSSK1XMK1XM " << "K1XM CQ SS K1XM K1XM ";
        QTest::newRow("call then de") << "73AE0QDE " << "73 AE0Q DE ";
        QTest::newRow("seen call wins") << "KC2RGW TUKC2RGWK " << "KC2RGW TU KC2RGW K ";
        QTest::newRow("report") << "UR5NN5NN " << "UR 5NN 5NN ";
        QTest::newRow("question mark closes a piece") << "QRZ?DEK1XM " << "QRZ? DE K1XM ";
        QTest::newRow("short runs are left") << "ESK " << "ESK ";
        QTest::newRow("garble is left") << "XQZJVKWP " << "XQZJVKWP ";
        QTest::newRow("a callsign alone is left") << "VA3SP " << "VA3SP ";
        // The last word may still be growing.
        QTest::newRow("open last word") << "CQ CQSSK1XM" << "CQ CQSSK1XM";
    }

    void forms()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(formCwWords(text), expected);
    }

    // Forming again changes nothing more.
    void isStable()
    {
        const QString once = formCwWords(QStringLiteral("K1XM CQSSK1XMK1XM TU5NN73 "));
        QCOMPARE(formCwWords(once), once);
    }
};

QTEST_APPLESS_MAIN(TestCwWordFormer)
#include "tst_cwwordformer.moc"
