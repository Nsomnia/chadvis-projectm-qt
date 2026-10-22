#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QtTest>

#include <cstdio>
#include <vector>

int runTestLogger(int argc, char** argv);
int runTestConfigParsers(int argc, char** argv);
int runTestConfigLoader(int argc, char** argv);
int runTestConfig(int argc, char** argv);
int runTestFileUtils(int argc, char** argv);
int runTestVersion(int argc, char** argv);
int runTestSignal(int argc, char** argv);
int runTestCliArgs(int argc, char** argv);
int runTestRecorder(int argc, char** argv);
int runTestAudioFileDecoder(int argc, char** argv);
int runTestRenderQueue(int argc, char** argv);
int runTestPlaylist(int argc, char** argv);
int runTestVisualizerAudioTap(int argc, char** argv);
int runTestPresetScanner(int argc, char** argv);
int runTestPresetBridge(int argc, char** argv);
int runTestPresetManager(int argc, char** argv);
int runTestSunoEndpoints(int argc, char** argv);
int runTestSunoAuthFailure(int argc, char** argv);
int runTestAuthModule(int argc, char** argv);
int runTestStoredCredentialClassification(int argc, char** argv);
int runTestBrowserCookieImport(int argc, char** argv);
int runTestCredentialRestoreWorker(int argc, char** argv);
int runTestSunoLibraryManager(int argc, char** argv);
int runTestLyricsFetchQueue(int argc, char** argv);
int runTestSunoDatabase(int argc, char** argv);
int runTestAuthCoordinator(int argc, char** argv);
int runTestClipParser(int argc, char** argv);
int runTestDownloadQueue(int argc, char** argv);
int runTestCredentialStorePolicy(int argc, char** argv);
int runTestMangoCrypto(int argc, char** argv);
int runTestSunoDownloader(int argc, char** argv);
int runTestDownloadEntitlement(int argc, char** argv);
int runTestBoundedBody(int argc, char** argv);
int runTestSunoWorkspace(int argc, char** argv);
int runTestLyricsPipeline(int argc, char** argv);
int runTestLyricsExport(int argc, char** argv);
int runTestLyricTiming(int argc, char** argv);
int runTestPcmFormat(int argc, char** argv);
int runTestLoudnessAnalysis(int argc, char** argv);
int runTestPathSafety(int argc, char** argv);
int runTestSunoBridge(int argc, char** argv);

// Breadcrumbs on stderr, one line in and one line out per suite. qtest writes
// its own progress to stdout, and on the Windows lane no stdout from any test
// binary reaches the log at all -- run 39479544129 ended with no FAIL!, no
// Totals and no way to tell which of the 39 suites it was. stderr survives, so
// the name and the return code are printed there instead. A non-zero code
// names the failing suite; a missing trailing line names the suite that died.
static int runSuite(const char* name, int (*fn)(int, char**), int argc, char** argv) {
    std::fprintf(stderr, "[suite] %s\n", name);
    std::fflush(stderr);

    // Qt Test's own `-o file,txt` logger is the only way to see assertions on
    // the Windows lane: eleven test binaries produced not one line of stdout
    // there, even with fd 1 re-pointed at fd 2 (run 39496998294). stderr does
    // reach the log, so a failing suite's log is echoed into it.
    const QString logPath = QDir::tempPath() + QStringLiteral("/chadvis-suite-log.txt");
    QByteArray logSpec = logPath.toUtf8() + ",txt";  // Qt Test: -o file,format
    std::vector<char*> args(argv, argv + argc);
    args.push_back(const_cast<char*>("-o"));
    args.push_back(logSpec.data());
    args.push_back(nullptr);

    const int rc = fn(argc + 2, args.data());
    if (rc != 0) {
        QFile log(logPath);
        if (log.open(QIODevice::ReadOnly)) {
            const QByteArray body = log.readAll();
            std::fwrite(body.constData(), 1, static_cast<size_t>(body.size()), stderr);
        }
    }
    QFile::remove(logPath);

    std::fprintf(stderr, "[suite] %s -> %d\n", name, rc);
    return rc;
}

#define RUN_SUITE(name) runSuite(#name, &run##name, argc, argv)

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int status = 0;
    status |= RUN_SUITE(TestLogger);
    status |= RUN_SUITE(TestConfigParsers);
    status |= RUN_SUITE(TestConfigLoader);
    status |= RUN_SUITE(TestConfig);
    status |= RUN_SUITE(TestFileUtils);
    status |= RUN_SUITE(TestVersion);
    status |= RUN_SUITE(TestSignal);
    status |= RUN_SUITE(TestCliArgs);
    status |= RUN_SUITE(TestRecorder);
    status |= RUN_SUITE(TestAudioFileDecoder);
    status |= RUN_SUITE(TestRenderQueue);
    status |= RUN_SUITE(TestPlaylist);
    status |= RUN_SUITE(TestVisualizerAudioTap);
    status |= RUN_SUITE(TestPresetScanner);
    status |= RUN_SUITE(TestPresetBridge);
    status |= RUN_SUITE(TestPresetManager);
    status |= RUN_SUITE(TestSunoEndpoints);
    status |= RUN_SUITE(TestSunoAuthFailure);
    status |= RUN_SUITE(TestAuthModule);
    status |= RUN_SUITE(TestStoredCredentialClassification);
    status |= RUN_SUITE(TestBrowserCookieImport);
    status |= RUN_SUITE(TestCredentialRestoreWorker);
    status |= RUN_SUITE(TestSunoLibraryManager);
    status |= RUN_SUITE(TestLyricsFetchQueue);
    status |= RUN_SUITE(TestSunoDatabase);
    status |= RUN_SUITE(TestAuthCoordinator);
    status |= RUN_SUITE(TestClipParser);
    status |= RUN_SUITE(TestDownloadQueue);
    status |= RUN_SUITE(TestCredentialStorePolicy);
    status |= RUN_SUITE(TestMangoCrypto);
    status |= RUN_SUITE(TestSunoDownloader);
    status |= RUN_SUITE(TestDownloadEntitlement);
    status |= RUN_SUITE(TestBoundedBody);
    status |= RUN_SUITE(TestSunoWorkspace);
    status |= RUN_SUITE(TestLyricsPipeline);
    status |= RUN_SUITE(TestLyricsExport);
    status |= RUN_SUITE(TestLyricTiming);
    status |= RUN_SUITE(TestPcmFormat);
    status |= RUN_SUITE(TestLoudnessAnalysis);
    status |= RUN_SUITE(TestPathSafety);
    status |= RUN_SUITE(TestSunoBridge);

    // Printed after the last suite returns, so a crash after this line is
    // teardown rather than a test.
    std::fprintf(stderr, "[suite] all suites returned, status %d\n", status);

    return status;
}
