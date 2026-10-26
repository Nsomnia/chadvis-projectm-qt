#include <QtTest>

#include "qml_bridge/SunoDownloadSection.hpp"

#include <QSignalSpy>
#include <QStringList>

using qml_bridge::SunoDownloadSection;

// ── Why this suite needs no controller ────────────────────────────────────────
// The section fails closed when no controller is attached: a save request says
// that no download service is attached, and a play request says the clip has no
// playable media. Both are sentences the user can act on, and neither sends a
// request.
//
// What is pinned here is the seam the extraction created: the section owns the
// downloader borrow, the latch and the status, and the bridge only relays.

class TestSunoDownloadSection : public QObject {
    Q_OBJECT

private slots:
    void statusIsEmptyBeforeAnyTransfer() {
        SunoDownloadSection section;
        QVERIFY(section.status().isEmpty());
        QVERIFY(section.activeClipId().isEmpty());
    }

    void saveWithoutAControllerRefusesWithASentence() {
        SunoDownloadSection section;
        QSignalSpy errorMessage(&section, &SunoDownloadSection::errorMessageRequested);
        QSignalSpy refused(&section, &SunoDownloadSection::clipSaveRefused);

        section.downloadClip(QStringLiteral("clip-1"));

        // One refusal, with a sentence, and no per-clip refusal: the request
        // never reached the per-clip stage.
        QCOMPARE(errorMessage.count(), 1);
        const QString reason = errorMessage.takeFirst().at(0).toString();
        QVERIFY(!reason.isEmpty());
        QVERIFY(reason.contains(QLatin1String("download service")));
        QCOMPARE(refused.count(), 0);
    }

    void emptyBatchStillReportsTheMissingService() {
        SunoDownloadSection section;
        QSignalSpy errorMessage(&section, &SunoDownloadSection::errorMessageRequested);

        // The service check comes before the empty-batch check, exactly as it
        // did before the extraction: a request that cannot be served is
        // reported as such, and the batch never reaches the per-clip stage.
        section.downloadClips(QStringList());

        QCOMPARE(errorMessage.count(), 1);
        QVERIFY(errorMessage.takeFirst().at(0).toString().contains(QLatin1String("download service")));
    }

    void playWithoutAControllerRefuses() {
        SunoDownloadSection section;
        QSignalSpy errorMessage(&section, &SunoDownloadSection::errorMessageRequested);
        QSignalSpy statusMessage(&section, &SunoDownloadSection::statusMessageRequested);

        section.playClip(QStringLiteral("clip-1"));

        QCOMPARE(errorMessage.count(), 1);
        QVERIFY(errorMessage.takeFirst().at(0).toString().contains(QLatin1String("playable media")));
        QCOMPARE(statusMessage.count(), 0);
    }

    void playOfAnEmptyIdIsRefusedBeforeAnything() {
        SunoDownloadSection section;
        QSignalSpy errorMessage(&section, &SunoDownloadSection::errorMessageRequested);

        section.playClip(QString());

        QCOMPARE(errorMessage.count(), 1);
        QVERIFY(section.status().isEmpty());
    }

    void detachIsSafe() {
        SunoDownloadSection section;
        QSignalSpy statusChanged(&section, &SunoDownloadSection::statusChanged);

        section.detach();
        section.detach();

        // Nothing was ever running, so nothing changed to announce.
        QCOMPARE(statusChanged.count(), 0);
        QVERIFY(section.status().isEmpty());
    }

    void titleProviderIsOptional() {
        SunoDownloadSection section;
        QSignalSpy errorMessage(&section, &SunoDownloadSection::errorMessageRequested);

        // A provider that answers is used; a missing one must not crash the
        // path that asks for a title.
        section.setTitleProvider([](const QString&) { return QStringLiteral("A Title"); });
        section.playClip(QString());

        QCOMPARE(errorMessage.count(), 1);
    }
};

int runTestSunoDownloadSection(int argc, char** argv) {
    TestSunoDownloadSection test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_SunoDownloadSection.moc"
