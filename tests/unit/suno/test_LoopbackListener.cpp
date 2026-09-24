#include <QtTest>
#include <QSignalSpy>
#include <QTcpSocket>

#include "suno/auth/oauth/LoopbackListener.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>

using namespace vc::suno::auth::oauth;

namespace {

struct RawRequestResult {
    QByteArray response;
    int accepted = 0;
    int failed = 0;
    QString reason;
};

QByteArray requestLine(const QByteArray& method, const QByteArray& target,
                       quint16 port, const QByteArray& host = {}) {
    return method + ' ' + target + " HTTP/1.1\r\nHost: " +
           (host.isEmpty() ? "127.0.0.1:" + QByteArray::number(port) : host) +
           "\r\nConnection: close\r\n\r\n";
}

RawRequestResult sendRaw(LoopbackListener& listener, const QByteArray& request,
                        bool expectSuccess = false) {
    RawRequestResult result;
    QSignalSpy accepted(&listener, &LoopbackListener::requestReceived);
    QSignalSpy rejected(&listener, &LoopbackListener::failed);

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, listener.port());
    if (!socket.waitForConnected(2000)) return result;
    socket.write(request);
    if (!socket.flush()) return result;

    QElapsedTimer elapsed;
    elapsed.start();
    const auto pumpUntil = [&](const auto& predicate) {
        while (!predicate() && elapsed.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return predicate();
    };

    if (expectSuccess) {
        if (!pumpUntil([&] { return accepted.count() == 1; })) return result;
        pumpUntil([&] { return socket.bytesAvailable() > 0; });
        result.response = socket.readAll();
        pumpUntil([&] { return socket.state() == QAbstractSocket::UnconnectedState; });
    } else {
        if (!pumpUntil([&] { return rejected.count() == 1; })) return result;
        pumpUntil([&] { return socket.state() == QAbstractSocket::UnconnectedState; });
        result.response = socket.readAll();
        if (!rejected.isEmpty()) {
            result.reason = rejected.first().at(0).toString();
        }
    }
    result.accepted = accepted.count();
    result.failed = rejected.count();
    return result;
}

} // namespace

class TestLoopbackListener : public QObject {
    Q_OBJECT

private slots:
    void startBindsEphemeralLocalhostPort() {
        LoopbackListener listener;
        QVERIFY(!listener.start(QStringLiteral("not-absolute")).has_value());
        QVERIFY(!listener.start(QString()).has_value());

        QVERIFY(listener.start(QStringLiteral("/oauth/callback")).has_value());
        QVERIFY(listener.port() != 0);
        QCOMPARE(listener.expectedPath(), QStringLiteral("/oauth/callback"));
        listener.stop();
    }

    void acceptsValidCallbackAndReturnsFixedPage() {
        LoopbackListener listener;
        QVERIFY(listener.start(QStringLiteral("/oauth/callback")).has_value());

        QSignalSpy accepted(&listener, &LoopbackListener::requestReceived);
        QSignalSpy rejected(&listener, &LoopbackListener::failed);
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, listener.port());
        QVERIFY(socket.waitForConnected(2000));

        const QByteArray marker = "query-secret-marker";
        socket.write(requestLine("GET", "/oauth/callback?state=state-value&code=" + marker,
                                 listener.port()));
        QVERIFY(socket.flush());

        QTRY_VERIFY(accepted.count() == 1 || rejected.count() == 1);
        if (!rejected.isEmpty()) {
            qInfo("request rejection: %s", qPrintable(rejected.first().at(0).toString()));
        }
        QCOMPARE(rejected.count(), 0);
        QCOMPARE(accepted.count(), 1);
        const QUrl callback = qvariant_cast<QUrl>(accepted.first().at(0));
        QCOMPARE(callback.path(), QStringLiteral("/oauth/callback"));
        QCOMPARE(callback.query(QUrl::FullyEncoded),
                 QStringLiteral("state=state-value&code=") + QString::fromLatin1(marker));

        QTRY_VERIFY(socket.bytesAvailable() > 0);
        const QByteArray response = socket.readAll();
        QVERIFY(response.startsWith("HTTP/1.1 200 OK\r\n"));
        QVERIFY(response.contains("\r\nConnection: close\r\n"));
        QVERIFY(response.contains("\r\nCache-Control: no-store\r\n"));
        QVERIFY(response.contains("Sign-in complete"));
        QVERIFY(!response.contains(marker));
        QVERIFY(!response.contains("state-value"));
    }

    void rejectsMalformedRequestsSilently() {
        struct Case {
            const char* name;
            QByteArray request;
        };
        QList<Case> cases;

        LoopbackListener probe;
        QVERIFY(probe.start(QStringLiteral("/oauth/callback")).has_value());
        const auto port = QByteArray::number(probe.port());
        const QByteArray host = "127.0.0.1:" + port;
        const QByteArray marker = "do-not-echo-this-secret";

        cases.append({"wrong method", requestLine("POST", "/oauth/callback?state=x", probe.port())});
        cases.append({"wrong path", requestLine("GET", "/different?state=x", probe.port())});
        cases.append({"wrong port", requestLine("GET", "/oauth/callback?state=x", probe.port(),
                                                "127.0.0.1:" + QByteArray::number(probe.port() + 1))});
        cases.append({"localhost host", requestLine("GET", "/oauth/callback?state=x", probe.port(),
                                                    "localhost:" + port)});
        cases.append({"absolute target", requestLine(
            "GET", "http://127.0.0.1:" + port + "/oauth/callback?state=" + marker, probe.port())});
        cases.append({"asterisk target", requestLine("GET", "*", probe.port())});
        cases.append({"duplicate query key", requestLine(
            "GET", "/oauth/callback?state=one&state=two&cookie=" + marker, probe.port())});
        cases.append({"body", "POST /oauth/callback?state=x HTTP/1.1\r\nHost: " + host +
                                  "\r\nContent-Length: 4\r\n\r\nbody"});
        cases.append({"transfer encoding", requestLine(
            "GET", "/oauth/callback?state=x", probe.port())
                              .replace("Connection: close\r\n",
                                       "Transfer-Encoding: chunked\r\nConnection: close\r\n")});
        const QByteArray oversizedUrl =
            "/oauth/callback?state=x&padding=" + QByteArray(LoopbackListener::kMaxUrlLengthBytes, 'a');
        cases.append({"oversize url", requestLine("GET", oversizedUrl, probe.port())});

        for (const auto& testCase : std::as_const(cases)) {
            qInfo("case: %s", testCase.name);
            const RawRequestResult result = sendRaw(probe, testCase.request);
            QCOMPARE(result.accepted, 0);
            QCOMPARE(result.failed, 1);
            QVERIFY(result.response.isEmpty());
            QVERIFY(!result.reason.isEmpty());
            QVERIFY(!result.reason.contains(QString::fromLatin1(marker)));
            QVERIFY(!result.reason.contains(QStringLiteral("state=")));
        }
    }

    void ignoresPipelinedExtraRequest() {
        LoopbackListener listener;
        QVERIFY(listener.start(QStringLiteral("/oauth/callback")).has_value());
        QByteArray pipeline = requestLine("GET", "/oauth/callback?state=first", listener.port());
        pipeline += requestLine("GET", "/oauth/callback?state=second", listener.port());

        QSignalSpy accepted(&listener, &LoopbackListener::requestReceived);
        QSignalSpy rejected(&listener, &LoopbackListener::failed);
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, listener.port());
        QVERIFY(socket.waitForConnected(2000));
        socket.write(pipeline);
        QVERIFY(socket.flush());

        QTRY_VERIFY(accepted.count() == 1 || rejected.count() == 1);
        if (!rejected.isEmpty()) {
            qInfo("request rejection: %s", qPrintable(rejected.first().at(0).toString()));
        }
        QCOMPARE(rejected.count(), 0);
        QCOMPARE(accepted.count(), 1);
        const QUrl first = qvariant_cast<QUrl>(accepted.first().at(0));
        QCOMPARE(first.query(QUrl::FullyEncoded), QStringLiteral("state=first"));
        QTRY_VERIFY(socket.bytesAvailable() > 0);
    }
};

int runTestLoopbackListener(int argc, char** argv) {
    TestLoopbackListener test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_LoopbackListener.moc"
