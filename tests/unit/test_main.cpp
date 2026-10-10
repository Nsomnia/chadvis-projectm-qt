#include <QCoreApplication>
#include <QtTest>

int runTestLogger(int argc, char** argv);
int runTestConfigParsers(int argc, char** argv);
int runTestConfigLoader(int argc, char** argv);
int runTestFileUtils(int argc, char** argv);
int runTestVersion(int argc, char** argv);
int runTestSignal(int argc, char** argv);
int runTestRecorder(int argc, char** argv);
int runTestAudioFileDecoder(int argc, char** argv);
int runTestRenderQueue(int argc, char** argv);
int runTestPlaylist(int argc, char** argv);
int runTestPresetScanner(int argc, char** argv);
int runTestPresetBridge(int argc, char** argv);
int runTestSunoEndpoints(int argc, char** argv);
int runTestAuthModule(int argc, char** argv);
int runTestStoredCredentialClassification(int argc, char** argv);
int runTestCredentialRestoreWorker(int argc, char** argv);
int runTestSunoLibraryManager(int argc, char** argv);
int runTestAuthCoordinator(int argc, char** argv);
int runTestLoopbackListener(int argc, char** argv);
int runTestOAuthLoginService(int argc, char** argv);
int runTestClipParser(int argc, char** argv);
int runTestDownloadQueue(int argc, char** argv);
int runTestCredentialStorePolicy(int argc, char** argv);
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

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int status = 0;
    status |= runTestLogger(argc, argv);
    status |= runTestConfigParsers(argc, argv);
    status |= runTestConfigLoader(argc, argv);
    status |= runTestFileUtils(argc, argv);
    status |= runTestVersion(argc, argv);
    status |= runTestSignal(argc, argv);
    status |= runTestRecorder(argc, argv);
    status |= runTestAudioFileDecoder(argc, argv);
    status |= runTestRenderQueue(argc, argv);
    status |= runTestPlaylist(argc, argv);
    status |= runTestPresetScanner(argc, argv);
    status |= runTestPresetBridge(argc, argv);
    status |= runTestSunoEndpoints(argc, argv);
    status |= runTestAuthModule(argc, argv);
    status |= runTestStoredCredentialClassification(argc, argv);
    status |= runTestCredentialRestoreWorker(argc, argv);
    status |= runTestSunoLibraryManager(argc, argv);
    status |= runTestAuthCoordinator(argc, argv);
    status |= runTestLoopbackListener(argc, argv);
    status |= runTestOAuthLoginService(argc, argv);
    status |= runTestClipParser(argc, argv);
    status |= runTestDownloadQueue(argc, argv);
    status |= runTestCredentialStorePolicy(argc, argv);
    status |= runTestSunoDownloader(argc, argv);
    status |= runTestDownloadEntitlement(argc, argv);
    status |= runTestBoundedBody(argc, argv);
    status |= runTestSunoWorkspace(argc, argv);
    status |= runTestLyricsPipeline(argc, argv);
    status |= runTestLyricsExport(argc, argv);
    status |= runTestLyricTiming(argc, argv);
    status |= runTestPcmFormat(argc, argv);
    status |= runTestLoudnessAnalysis(argc, argv);
    status |= runTestPathSafety(argc, argv);

    return status;
}
