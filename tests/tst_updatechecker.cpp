#include "UpdateChecker.h"

#include <QTest>

using namespace ts590;

class TestUpdateChecker : public QObject
{
    Q_OBJECT
private slots:
    void compare_data()
    {
        QTest::addColumn<QString>("candidate");
        QTest::addColumn<QString>("current");
        QTest::addColumn<bool>("newer");
        QTest::newRow("point") << "0.15.1" << "0.15" << true;
        QTest::newRow("minor") << "0.16" << "0.15.1" << true;
        QTest::newRow("same") << "0.15" << "0.15.0" << false;
        QTest::newRow("older") << "0.14" << "0.15" << false;
        QTest::newRow("numeric not text") << "0.15.10" << "0.15.9" << true;
        QTest::newRow("leading v") << "v0.16" << "0.15" << true;
        QTest::newRow("suffix") << "0.16-beta" << "0.15" << true;
        QTest::newRow("empty") << "" << "0.15" << false;
    }
    void compare()
    {
        QFETCH(QString, candidate);
        QFETCH(QString, current);
        QFETCH(bool, newer);
        QCOMPARE(UpdateChecker::isNewer(candidate, current), newer);
    }
};

QTEST_GUILESS_MAIN(TestUpdateChecker)
#include "tst_updatechecker.moc"
