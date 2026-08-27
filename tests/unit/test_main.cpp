#include <QCoreApplication>
#include <QtTest>

int runTestLogger(int argc, char** argv);
int runTestConfigParsers(int argc, char** argv);
int runTestAuthModule(int argc, char** argv);
int runTestClipParser(int argc, char** argv);
int runTestDownloadQueue(int argc, char** argv);

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int status = 0;
    status |= runTestLogger(argc, argv);
    status |= runTestConfigParsers(argc, argv);
    status |= runTestAuthModule(argc, argv);
    status |= runTestClipParser(argc, argv);
    status |= runTestDownloadQueue(argc, argv);

    return status;
}
