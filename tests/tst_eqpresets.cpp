#include "EqAutoLoader.h"
#include "EqPresetLibrary.h"
#include "Radio.h"
#include "SimulatorTransport.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace ts590;

namespace {

EqLevels ramp(int start)
{
    EqLevels levels;
    for (int band = 0; band < kEqBandCount; ++band)
        levels[band] = std::min(start + band, kEqRawMax);
    return levels;
}

}   // namespace

class TestEqPresets : public QObject
{
    Q_OBJECT

private slots:
    void savesListsAndReloads()
    {
        QTemporaryDir dir;
        {
            EqPresetLibrary library(dir.path());
            QVERIFY(library.save(EqType::Rx, QStringLiteral("rag chew"), ramp(0)));
            QVERIFY(library.save(EqType::Rx, QStringLiteral("DX/punchy: v2"), ramp(4)));
            QVERIFY(library.save(EqType::Tx, QStringLiteral("Bass"), ramp(2)));
            QVERIFY(!library.save(EqType::Tx, QStringLiteral("   "), ramp(2)));
        }
        EqPresetLibrary reloaded(dir.path());
        QCOMPARE(reloaded.names(EqType::Rx),
                 QStringList({QStringLiteral("DX/punchy: v2"), QStringLiteral("rag chew")}));
        QCOMPARE(reloaded.names(EqType::Tx), QStringList({QStringLiteral("Bass")}));
        QCOMPARE(reloaded.levels(EqType::Rx, QStringLiteral("DX/punchy: v2")), std::optional(ramp(4)));
        QVERIFY(!reloaded.levels(EqType::Tx, QStringLiteral("rag chew")));
    }

    void overwriteAndRemoveClearsBindings()
    {
        QTemporaryDir dir;
        EqPresetLibrary library(dir.path());
        QVERIFY(library.save(EqType::Rx, QStringLiteral("A"), ramp(0)));
        QVERIFY(library.save(EqType::Rx, QStringLiteral("A"), ramp(3)));
        QCOMPARE(library.names(EqType::Rx).size(), 1);
        QCOMPARE(library.levels(EqType::Rx, QStringLiteral("A")), std::optional(ramp(3)));

        library.setBinding(EqType::Rx, EqMode::SsbData, QStringLiteral("A"));
        QSignalSpy bindings(&library, &EqPresetLibrary::bindingsChanged);
        QVERIFY(library.remove(EqType::Rx, QStringLiteral("A")));
        QVERIFY(library.names(EqType::Rx).isEmpty());
        QCOMPARE(library.binding(EqType::Rx, EqMode::SsbData), QString());
        QCOMPARE(bindings.size(), 1);
        QCOMPARE(EqPresetLibrary(dir.path()).names(EqType::Rx), QStringList());
    }

    void bindingsPersist()
    {
        QTemporaryDir dir;
        {
            EqPresetLibrary library(dir.path());
            library.save(EqType::Tx, QStringLiteral("Speech"), ramp(1));
            library.setBinding(EqType::Tx, EqMode::Ssb, QStringLiteral("Speech"));
            library.setBinding(EqType::Rx, EqMode::Am, QStringLiteral("Speech"));
            library.setBinding(EqType::Rx, EqMode::Am, QString());
        }
        EqPresetLibrary reloaded(dir.path());
        QCOMPARE(reloaded.binding(EqType::Tx, EqMode::Ssb), QStringLiteral("Speech"));
        QCOMPARE(reloaded.binding(EqType::Tx, EqMode::SsbData), QString());
        QCOMPARE(reloaded.binding(EqType::Rx, EqMode::Am), QString());
    }

    void importExportRoundTrip()
    {
        QTemporaryDir dir;
        EqPresetLibrary library(dir.path() + QStringLiteral("/lib"));
        library.save(EqType::Rx, QStringLiteral("Mine"), ramp(5));
        const QString file = dir.path() + QStringLiteral("/mine.json");
        QVERIFY(library.exportFile(EqType::Rx, QStringLiteral("Mine"), file));

        EqPresetLibrary other(dir.path() + QStringLiteral("/other"));
        const auto name = other.importFile(EqType::Tx, file);
        QCOMPARE(name, std::optional(QStringLiteral("Mine")));
        QCOMPARE(other.levels(EqType::Tx, QStringLiteral("Mine")), std::optional(ramp(5)));

        QFile junk(dir.path() + QStringLiteral("/junk.json"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("{\"hello\": 1}");
        junk.close();
        QString error;
        QVERIFY(!other.importFile(EqType::Tx, junk.fileName(), &error));
        QVERIFY(!error.isEmpty());
    }

    // The simulator starts in USB with both SSB-DATA curves on U.
    void autoLoadsBoundPresetIntoUser()
    {
        QTemporaryDir dir;
        EqPresetLibrary library(dir.path());
        library.save(EqType::Rx, QStringLiteral("Data RX"), ramp(3));
        library.setBinding(EqType::Rx, EqMode::SsbData, QStringLiteral("Data RX"));

        Radio radio;
        EqAutoLoader loader(&radio, &library);
        QSignalSpy applied(&loader, &EqAutoLoader::applied);
        auto transport = std::make_unique<SimulatorTransport>();
        SimulatorTransport *sim = transport.get();
        radio.connectTransport(std::move(transport));
        radio.setMeterPolling(false);
        QTRY_COMPARE(radio.state(), Radio::State::Connected);
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        // SSB curve is OFF and unbound: nothing written.
        QCOMPARE(applied.size(), 0);

        radio.setDataMode(true);
        QTRY_COMPARE(sim->eqLevels(EqType::Rx), ramp(3));
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        QCOMPARE(applied.size(), 1);
        QCOMPARE(radio.eqLevels(EqType::Rx), std::optional(ramp(3)));
        // TX has no binding, so its U curve is untouched.
        QCOMPARE(sim->eqLevels(EqType::Tx), flatEqLevels());
    }

    void bindingCurrentModeAppliesImmediately()
    {
        QTemporaryDir dir;
        EqPresetLibrary library(dir.path());
        library.save(EqType::Tx, QStringLiteral("Data TX"), ramp(1));

        Radio radio;
        EqAutoLoader loader(&radio, &library);
        auto transport = std::make_unique<SimulatorTransport>();
        SimulatorTransport *sim = transport.get();
        radio.connectTransport(std::move(transport));
        radio.setMeterPolling(false);
        QTRY_COMPARE(radio.state(), Radio::State::Connected);
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        radio.setDataMode(true);
        QTRY_COMPARE(radio.eqCurve(EqType::Tx), std::optional(EqCurve::User));
        QTRY_COMPARE(radio.link()->pendingCount(), 0);

        library.setBinding(EqType::Tx, EqMode::SsbData, QStringLiteral("Data TX"));
        QTRY_COMPARE(sim->eqLevels(EqType::Tx), ramp(1));
    }
};

QTEST_GUILESS_MAIN(TestEqPresets)
#include "tst_eqpresets.moc"
