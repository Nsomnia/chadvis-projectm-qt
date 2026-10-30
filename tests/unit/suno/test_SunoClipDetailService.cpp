#include <QtTest>

#include "suno/SunoClipDetailService.hpp"

#include <QJsonArray>
#include <QJsonObject>

using namespace vc::suno;

namespace {

QJsonObject clipJson(const QString& id, const QString& title)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("status"), QStringLiteral("complete")}};
}

} // namespace

class TestSunoClipDetailService : public QObject
{
    Q_OBJECT

private slots:
    void attributionCarriesTheAncestryAndItsRelationship()
    {
        // Captured shape: `source_clips[]` entries wrap a clip and say COV|EX.
        const QJsonObject root{
                {QStringLiteral("source_clips"),
                 QJsonArray{
                         QJsonObject{
                                 {QStringLiteral("clip"),
                                  clipJson(QStringLiteral("parent-1"),
                                           QStringLiteral("the original"))},
                                 {QStringLiteral("relationship"), QStringLiteral("COV")}},
                         QJsonObject{
                                 {QStringLiteral("clip"),
                                  clipJson(QStringLiteral("parent-2"),
                                           QStringLiteral("the extension"))},
                                 {QStringLiteral("relationship"), QStringLiteral("EX")}},
                 }}};

        const auto sources = SunoClipDetailService::parseAttribution(root);
        QVERIFY(sources.has_value());
        QCOMPARE(sources->size(), 2);
        QCOMPARE(QString::fromStdString(sources->at(0).clip.id), QStringLiteral("parent-1"));
        QCOMPARE(sources->at(0).relationship, QStringLiteral("COV"));
        QCOMPARE(sources->at(1).relationship, QStringLiteral("EX"));
    }

    void anOriginalSongSimplyHasNoAttribution()
    {
        // Absence is the common case, not an error: an original is not "broken".
        const auto sources = SunoClipDetailService::parseAttribution(QJsonObject{});
        QVERIFY(sources.has_value());
        QVERIFY(sources->isEmpty());
    }

    void commentsReadTheNestedAuthorAndTheFlatFallback()
    {
        const QJsonObject root{
                {QStringLiteral("total_count"), 12},
                {QStringLiteral("allow_comment"), true},
                {QStringLiteral("results"),
                 QJsonArray{
                         QJsonObject{
                                 {QStringLiteral("text"), QStringLiteral("great hook")},
                                 {QStringLiteral("like_count"), 4},
                                 {QStringLiteral("user"),
                                  QJsonObject{{QStringLiteral("display_name"),
                                               QStringLiteral("Ada")}}}},
                         QJsonObject{{QStringLiteral("content"), QStringLiteral("again")},
                                     {QStringLiteral("user_name"), QStringLiteral("Bo")}},
                 }}};

        const auto comments = SunoClipDetailService::parseComments(root);
        QVERIFY(comments.has_value());
        QCOMPARE(comments->size(), 2);
        QCOMPARE(comments->at(0).author, QStringLiteral("Ada"));
        QCOMPARE(comments->at(0).text, QStringLiteral("great hook"));
        QCOMPARE(comments->at(0).likeCount, 4);
        QCOMPARE(comments->at(1).author, QStringLiteral("Bo"));
        QCOMPARE(comments->at(1).text, QStringLiteral("again"));
    }

    void remixCountTakesTheNumberOrTheArrayLength()
    {
        QCOMPARE(SunoClipDetailService::parseRemixCount(
                         QJsonObject{{QStringLiteral("count"), 3}}),
                 3);
        QCOMPARE(SunoClipDetailService::parseRemixCount(
                         QJsonObject{{QStringLiteral("clips"),
                                      QJsonArray{clipJson(QStringLiteral("a"),
                                                          QStringLiteral("x"))}}}),
                 1);
        // No denominator at all is a real zero for this route, not an error.
        QCOMPARE(SunoClipDetailService::parseRemixCount(QJsonObject{}), 0);
    }

    void similarClipsAreUnwrapped()
    {
        const QJsonObject root{
                {QStringLiteral("clips"),
                 QJsonArray{QJsonObject{{QStringLiteral("clip"),
                                         clipJson(QStringLiteral("s1"),
                                                  QStringLiteral("sounds alike"))}}}}};
        const auto similar = SunoClipDetailService::parseSimilar(root);
        QVERIFY(similar.has_value());
        QCOMPARE(similar->size(), 1);
        QCOMPARE(QString::fromStdString(similar->at(0).id), QStringLiteral("s1"));
    }

    void anEmptyLoadIsANoOp()
    {
        SunoClipDetailService service(nullptr);
        QSignalSpy failed(&service, &SunoClipDetailService::failed);
        service.load(QString());
        // No id and no client: nothing to ask about, and nothing to report.
        QCOMPARE(failed.count(), 0);
        QVERIFY(service.detail().clipId.isEmpty());
    }
};

QTEST_MAIN(TestSunoClipDetailService)
#include "test_SunoClipDetailService.moc"
