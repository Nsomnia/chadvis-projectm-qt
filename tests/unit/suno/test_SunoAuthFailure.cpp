#include <QtTest>

#include "suno/SunoAuthFailure.hpp"

using namespace vc::suno;

// The classifier is the one gate between an HTTP reply and a full session
// teardown: a true verdict drops the bearer, flushes pending auth work, sets
// NeedsReauth and emits needsReauth(). It used to substring-match
// "401"/"Unauthorized" in the error message, so any byte count, clip id or
// URL segment containing those digits signed out a healthy session. The
// string parameter is gone, so the old failure shape cannot be written
// again — the status is the only input, and a caller with no status (a
// transport error reads as 0; the old lyrics call site passed -1) is by
// construction not an auth failure.
class TestSunoAuthFailure : public QObject {
    Q_OBJECT

private slots:
    void authFailureStatusesClassify();
    void nothingElseDoes();
};

void TestSunoAuthFailure::authFailureStatusesClassify() {
    QVERIFY(isAuthFailure(401));
    QVERIFY(isAuthFailure(403));
}

void TestSunoAuthFailure::nothingElseDoes() {
    // 0 is the shape handleNetworkError actually sees for a transport error
    // (the status attribute is invalid and toInt() defaults); -1 was the
    // "no status exists" value the old lyrics call site passed. The rest are
    // the near neighbours: a 402 is not a 401, a 404 is not auth death, and
    // a 429 rate limit must never sign anyone out.
    const int neverAuth[] = {-1, 0, 200, 204, 302, 400, 402, 404, 429, 500, 503};
    for (const int status : neverAuth) {
        QVERIFY2(!isAuthFailure(status),
                 qPrintable(QStringLiteral("status %1 was misclassified as an "
                                           "auth failure")
                                    .arg(status)));
    }
}

#include "test_SunoAuthFailure.moc"

int runTestSunoAuthFailure(int argc, char** argv) {
    TestSunoAuthFailure tc;
    return QTest::qExec(&tc, argc, argv);
}
