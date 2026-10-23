#include <QAudioBuffer>
#include <QAudioFormat>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include "audio/AudioEngine.hpp"

#include <atomic>
#include <string>
#include <thread>

using namespace vc;

namespace {

/// An interleaved Int16 sink buffer built by hand, so these tests need no audio
/// device and no media file. The rate is the payload processAudioBuffer()
/// observes; @p frames decides whether the byte count fits the conversion window.
QAudioBuffer makeBuffer(int sampleRate, int frames) {
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    const QByteArray bytes(frames * 2 * static_cast<int>(sizeof(qint16)), Qt::Uninitialized);
    return QAudioBuffer(bytes, format);
}

/// Engine with a session playlist in a temp dir, so nothing touches the real
/// config directory. init() is required rather than optional: the destructor
/// calls stop(), which dereferences the player init() creates.
struct EngineFixture {
    explicit EngineFixture(const QTemporaryDir& dir) : engine(fs::path(dir.path().toStdString()))
    {
        expectsInitOk = engine.init(AudioConfig{}).isOk();
    }
    AudioEngine engine;
    bool expectsInitOk{false};
};

} // namespace

class TestAudioEngine : public QObject {
    Q_OBJECT

private slots:
    /// T0154. The sink rate is learned on the audio callback thread, but the
    /// sentence describing it must be built on the engine's own thread. The
    /// signal is connected DIRECTLY here on purpose: an AutoConnection would move
    /// the assertion to the receiving thread and pass even if the engine emitted
    /// from the callback thread, which is the defect.
    void sinkRateReachesTheStatusWithoutBuildingItOnTheCallbackThread() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        EngineFixture fixture(dir);
        QVERIFY(fixture.expectsInitOk);

        std::atomic<int> statusCount{0};
        std::atomic<QThread*> deliveredOn{nullptr};
        std::atomic<bool> mentionsRate{false};
        connect(&fixture.engine, &AudioEngine::audioOutputStatusChanged, &fixture.engine,
                [&](const QString& status) {
                    deliveredOn.store(QThread::currentThread());
                    mentionsRate.store(status.contains(QStringLiteral("sink: 44100 Hz")));
                    statusCount.fetch_add(1);
                },
                Qt::DirectConnection);

        const QAudioBuffer buffer = makeBuffer(44100, 256);
        std::thread worker([&] { fixture.engine.submitAudioBuffer(buffer); });
        worker.join();

        // Nothing was built, and therefore nothing was emitted, on the callback
        // thread; the observation is an atomic and the sentence is queued.
        QCOMPARE(statusCount.load(), 0);
        QTRY_COMPARE(statusCount.load(), 1);
        QCOMPARE(deliveredOn.load(), QThread::currentThread());
        QVERIFY2(mentionsRate.load(), "the status never reported the rate the sink delivered");

        // The same rate is not a change, so the queued half does not fire again.
        std::thread again([&] { fixture.engine.submitAudioBuffer(buffer); });
        again.join();
        QCoreApplication::processEvents();
        QCOMPARE(statusCount.load(), 1);
    }

    /// T0155. A fault found on the callback thread is recorded atomically and
    /// reported once, off that thread; the recovery signal takes the same route
    /// rather than being emitted from the callback. Log and signal are asserted
    /// separately: the counter moves immediately, the report does not.
    void aFaultIsRecordedOnTheCallbackThreadAndReportedOnceOffIt() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        EngineFixture fixture(dir);
        QVERIFY(fixture.expectsInitOk);
        AudioEngine& engine = fixture.engine;

        std::atomic<int> errorCount{0};
        std::atomic<QThread*> deliveredOn{nullptr};
        std::atomic<bool> sawDetail{false};
        std::atomic<bool> sawRecovery{false};
        connect(&engine, &AudioEngine::errorSignal, &engine,
                [&](const std::string& error) {
                    deliveredOn.store(QThread::currentThread());
                    if (error.empty()) {
                        sawRecovery.store(true);
                    } else if (error.find("PCM conversion needs") != std::string::npos) {
                        sawDetail.store(true);
                    }
                    errorCount.fetch_add(1);
                },
                Qt::DirectConnection);

        // 16384 stereo frames is 32768 interleaved samples: larger than any
        // conversion window this engine can resize to, so the refusal is
        // DestinationTooSmall and does not depend on the configured buffer size.
        const QAudioBuffer oversized = makeBuffer(44100, 16384);
        std::thread worker([&] { engine.submitAudioBuffer(oversized); });
        worker.join();

        QCOMPARE(engine.oversizedBufferCount(), u64{1});
        QCOMPARE(errorCount.load(), 0);
        QTRY_COMPARE(errorCount.load(), 1);
        QCOMPARE(deliveredOn.load(), QThread::currentThread());
        QVERIFY(sawDetail.load());

        // The callback keeps dropping buffers, but the report is rate limited, so
        // the queued half stays at one for this fault.
        std::thread second([&] { engine.submitAudioBuffer(oversized); });
        second.join();
        QCOMPARE(engine.oversizedBufferCount(), u64{2});
        QCoreApplication::processEvents();
        QCOMPARE(errorCount.load(), 1);

        // The first buffer that converts clears the fault with the documented
        // empty string, again queued rather than emitted from the callback.
        const QAudioBuffer fits = makeBuffer(44100, 256);
        std::thread recovery([&] { engine.submitAudioBuffer(fits); });
        recovery.join();
        QCOMPARE(errorCount.load(), 1);
        QTRY_VERIFY(sawRecovery.load());
        QCOMPARE(errorCount.load(), 2);
    }
};

#include "test_AudioEngine.moc"

int runTestAudioEngine(int argc, char** argv) {
    TestAudioEngine tc;
    return QTest::qExec(&tc, argc, argv);
}
