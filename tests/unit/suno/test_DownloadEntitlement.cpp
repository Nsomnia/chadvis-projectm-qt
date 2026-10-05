// test_DownloadEntitlement — the server's own "may this account save this
// clip" answer, consulted before anything else.
//
// The change under test adds `downloadEntitlementRefusal()` to
// SunoDownloader::download(), reading SunoClip::is_download_unlocked
// (`std::optional<bool>`) and SunoClip::download_disabled_reason, both newly
// parsed by ClipParser::parseClip.
//
// It exists because the gate that ran *first* — `content_type == "mp3"` on one
// captured media host — rejects 100% of the media URLs in the evidence corpus
// (40/40 observed entries are `m4a-opus` on a CloudFront origin; `mp3` never
// occurs). So every refusal a user could see was attributed to a local filter
// while the server's actual decision went unread: a clip Suno had already
// declined was reported as "no playable audio on the captured media host",
// which is the wrong diagnosis of a real fault.
//
// ── The judgement call, which is the reason this suite exists ───────────────
//
// ABSENT is deliberately NOT a refusal. That is the opposite of the reflexive
// "unknown is not permission" rule, so it is pinned from both directions:
//
//   absent         -> proceeds   (absentEntitlementIsNotARefusal)
//   present+false  -> refuses    (lockedClipWithReasonQuotesItVerbatim, ...)
//   present+true   -> proceeds   (unlockedClipProceeds)
//   present+garbage-> refuses    (presentButUnreadableValueIsLockedNotAbsent)
//
// The load-bearing risk is a one-character change nobody notices: flipping
// `value_or(true)` to `value_or(false)` refuses every clip persisted before
// this field was parsed — including a signed-in user's entire existing
// library — and buys no additional safety, because a refusal is already issued
// *alongside* a usable `media_urls` entry and the host/content-type filters do
// the withholding regardless. So `absentEntitlementIsNotARefusal` does not
// merely assert "not refused": it pins the ABSENT clip reaching the *format*
// gate and being refused *there*, with the exact format sentence. That is the
// only way to tell "proceeded past the entitlement gate" from "was refused by
// it", because both are refusals.
//
// ── Why the fake server, and what is asserted about it ──────────────────────
//
// The gate must cost nothing when it refuses. One that decided after starting
// a transfer would spend a metered resource on a clip the server had already
// declined. So every refusal case asserts `server.requests` is EMPTY — not
// merely that no reply came back — and that no `fileSaved` fired. The
// destination-claim case (`aRefusedClipClaimsNoDestination`) pins the same
// idea for local state: a refusal leaves no trace in `destOwner_` either.
//
// Nothing leaves the machine. Replies come from DownloadQueue's injectable
// ReplyFactory seam (FakeNetworkReply.hpp), exactly as test_SunoDownloader
// does, and there are no threads anywhere in this path.

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/Config.hpp"
#include "suno/ClipParser.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoDownloader.hpp"

#include "FakeNetworkReply.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace vc;
using namespace vc::suno;
using fake_net::FakeServer;

namespace {

// ── the two refusal sentences, pinned exactly ─────────────────────────────────
//
// Pinned as whole sentences rather than as substrings. Both sentences are the
// product of this change — the reason a user is told anything at all — and a
// substring assertion cannot distinguish "the reason is quoted" from "the
// reason is quoted somewhere in a longer, differently-shaped sentence that also
// speculates". A reword is then a one-line change here, deliberately.
//
// The no-reason sentence is the one carrying a claim the evidence does not
// support, so its hedge is asserted separately in
// `lockedClipWithNoReasonOffersTheAllowanceAsPossibilityNotFact`.

const QString kRefusalWithReason =
    QStringLiteral("Suno does not allow this clip to be downloaded (%1).");

const QString kRefusalNoReason = QStringLiteral(
    "Suno does not allow this clip to be downloaded. This can be a monthly "
    "download allowance rather than anything to do with this clip.");

/// The format gate's own sentence. Reached only when the entitlement gate let
/// the clip through, which is what makes it usable as a marker.
const QString kRefusalUnfetchableMedia = QStringLiteral(
    "This clip's audio is served in a format or from a host this client will "
    "not fetch, so there is nothing to save.");

/// Restores both config fields the downloader reads. The download directory is
/// a process-global config value, so a test that leaked it would redirect every
/// later download in the binary into a deleted temporary directory.
class EntitlementConfigGuard {
public:
    EntitlementConfigGuard()
        : path_(CONFIG.suno().downloadPath), format_(CONFIG.suno().downloadFormat) {
        CONFIG.suno().downloadFormat = SunoDownloadFormat::MP3;
    }
    ~EntitlementConfigGuard() {
        CONFIG.suno().downloadPath = path_;
        CONFIG.suno().downloadFormat = format_;
    }

private:
    fs::path path_;
    SunoDownloadFormat format_;
};

/// A clip this client WOULD fetch: an mp3 entry on the one captured media host,
/// status complete. Every gate after the entitlement one passes, so whatever
/// sentence comes back is attributable to the gate that produced it.
SunoClip fetchableClip(const std::string& id, const std::string& title) {
    SunoClip clip;
    clip.id = id;
    clip.title = title;
    clip.status = "complete";
    clip.display_name = "ChadVis Test";
    clip.media_urls = {{"https://audiopipe.suno.ai/" + id + ".mp3", "mp3", "streaming", ""}};
    return clip;
}

/// The captured shape of 40/40 observed media entries: `m4a-opus`, never mp3.
/// The local format filter rejects this, which is exactly what makes it the
/// probe for "did the entitlement gate run first, or the format gate?".
SunoClip opusOnlyClip(const std::string& id) {
    SunoClip clip;
    clip.id = id;
    clip.title = "Opus Only " + id;
    clip.status = "complete";
    clip.media_urls = {{"https://audiopipe.suno.ai/" + id + ".m4a", "m4a-opus",
                        "progressive", ""}};
    return clip;
}

/// Apply the server's refusal to a clip. `reason` empty models the 32-of-40
/// captured clips that are locked and say nothing about why.
SunoClip serverLocked(SunoClip clip, std::string reason = {}) {
    clip.is_download_unlocked = false;
    clip.download_disabled_reason = std::move(reason);
    return clip;
}

QByteArray payloadFor(const QString& id) {
    return QByteArray("chadvis-entitlement-audio-") + id.toUtf8();
}

QJsonObject objFrom(const char* json) {
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

/// One downloader, a fake reply factory, and a temporary download directory.
///
/// The audio engine is deliberately `nullptr`: every case here uses
/// DownloadAction::SaveOnly, which never reaches addAndPlay(). So this suite
/// also proves the gate has no dependency on the transport at all — and it
/// removes the audio-backend dependency the sibling downloader suite has a
/// skipped test about.
class Fixture {
public:
    Fixture() {
        if (!downloadDir_.isValid()) {
            return;
        }
        CONFIG.suno().downloadPath = downloadDir_.path().toStdString();
        if (!database_.init(":memory:")) {
            return;
        }
        downloader_ = std::make_unique<SunoDownloader>(database_, nullptr, server_.factory());
    }

    [[nodiscard]] bool valid() const { return downloader_ != nullptr; }
    [[nodiscard]] QString dir() const { return downloadDir_.path(); }
    [[nodiscard]] FakeServer& server() { return server_; }
    /// Returns a reference, but QSignalSpy needs `&fixture.downloader()`:
    /// its pointer-to-member constructor takes `const Object*` and a bare
    /// reference does not convert to one.
    [[nodiscard]] SunoDownloader& downloader() { return *downloader_; }

    /// Drive the one in-flight transfer to completion, so a case can assert an
    /// outcome rather than only that a request was issued.
    [[nodiscard]] bool succeedTransfer(const QString& clipId) {
        const int index = server_.requestIndexForPath(clipId + QStringLiteral(".mp3"));
        if (index < 0) {
            return false;
        }
        server_.replies[static_cast<std::size_t>(index)]->succeed(payloadFor(clipId));
        QTest::qWait(20);  // drain deleteLater, as the sibling suite does
        return true;
    }

private:
    QTemporaryDir downloadDir_;
    QTemporaryDir sessionDir_;
    SunoDatabase database_;
    // Declaration order is load-bearing: members are destroyed in reverse, so
    // `downloader_` dies before `server_` — the reply factory lambda borrows
    // the server and must outlive the queue that holds it.
    FakeServer server_;
    std::unique_ptr<SunoDownloader> downloader_;
};

}  // namespace

class TestDownloadEntitlement : public QObject {
    Q_OBJECT

private slots:
    // ── 1. ClipParser: presence is not truth ──────────────────────────────────

    /// The straightforward case: an explicit grant is recorded as a grant.
    void unlockedTrueIsPresentAndTrue() {
        auto parsed = ClipParser::parseClip(QJsonObject{
            {"id", "grant-true"},
            {"is_download_unlocked", true},
        });
        QVERIFY(parsed.has_value());
        QVERIFY(parsed->is_download_unlocked.has_value());
        QCOMPARE(*parsed->is_download_unlocked, true);
    }

    /// The load-bearing one. `false` must be recorded as PRESENT-and-false,
    /// because the gate's whole refusal rule is "present and false". A plain
    /// `bool` defaulting to false could not tell this apart from a payload that
    /// never carried the field, and collapsing the two would either refuse
    /// every legacy clip or honour every refusal — there is no third option.
    void unlockedFalseIsPresentAndFalse() {
        auto parsed = ClipParser::parseClip(QJsonObject{
            {"id", "deny-false"},
            {"is_download_unlocked", false},
        });
        QVERIFY(parsed.has_value());
        QVERIFY2(parsed->is_download_unlocked.has_value(),
                 "an explicit false was recorded as absent, which would silently "
                 "re-grant a clip the server just declined");
        QCOMPARE(*parsed->is_download_unlocked, false);
    }

    /// Absent is a THIRD state, not a synonym for false. Asserted as an
    /// inequality against both neighbours so the three-way distinction is
    /// stated rather than implied.
    void absentFieldIsUnsetRatherThanFalse() {
        auto parsed = ClipParser::parseClip(QJsonObject{{"id", "no-flag"}});
        QVERIFY(parsed.has_value());
        QVERIFY2(!parsed->is_download_unlocked.has_value(),
                 "a payload with no entitlement field must leave it unset; "
                 "defaulting to false here would refuse the entire pre-existing "
                 "library, which is the regression this design exists to avoid");
        QVERIFY(parsed->is_download_unlocked != std::optional<bool>{false});
        QVERIFY(parsed->is_download_unlocked == std::optional<bool>{});
    }

    /// An explicit JSON null is absence, not a denial. `QJsonValue::isNull()`
    /// covers it alongside `isUndefined()`, so the same skip covers both — pinned
    /// because a null is a real wire form distinct from an omitted key.
    void explicitNullIsTreatedAsAbsent() {
        auto parsed =
            ClipParser::parseClip(objFrom("{\"id\": \"nulled\", \"is_download_unlocked\": null}"));
        QVERIFY(parsed.has_value());
        QVERIFY2(!parsed->is_download_unlocked.has_value(),
                 "an explicit null was read as a denial rather than as absence");
        QVERIFY(parsed->is_download_unlocked == std::optional<bool>{});
    }

    /// String forms, which the captured corpus genuinely carries: `optBool`
    /// reads "True"/"False" case-insensitively, and the parser reuses it for
    /// this field rather than duplicating that logic.
    void capturedStringFormsAreAccepted_data() {
        QTest::addColumn<QString>("wire");
        QTest::addColumn<bool>("expectPresent");
        QTest::addColumn<bool>("expectValue");

        QTest::newRow("True")     << QStringLiteral("True")  << true  << true;
        QTest::newRow("true")     << QStringLiteral("true")  << true  << true;
        QTest::newRow("TRUE")     << QStringLiteral("TRUE")  << true  << true;
        QTest::newRow("False")    << QStringLiteral("False") << true  << false;
        QTest::newRow("false")    << QStringLiteral("false") << true  << false;
        QTest::newRow("FaLsE")    << QStringLiteral("FaLsE") << true  << false;
    }

    void capturedStringFormsAreAccepted() {
        QFETCH(QString, wire);
        QFETCH(bool, expectPresent);
        QFETCH(bool, expectValue);

        auto parsed = ClipParser::parseClip(QJsonObject{
            {"id", "string-form"},
            {"is_download_unlocked", wire},
        });
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->is_download_unlocked.has_value(), expectPresent);
        QVERIFY2(parsed->is_download_unlocked.has_value(),
                 "a string the parser recognises must not degrade to absence");
        QCOMPARE(*parsed->is_download_unlocked, expectValue);
    }

    /// The complement of the absent rule, and the reason the absent rule is not
    /// simply "be liberal". A field that IS present but unreadable is locked,
    /// not absent: the server stated something about this clip and the client
    /// could not read it, which is the "unknown is not permission" case. Only
    /// *omission* is absence.
    ///
    /// This is also the shape to watch if Suno ever changes the wire form: a
    /// numeric `1`/`0` would silence every download in the product rather than
    /// fail loudly. Defensible today (fail-closed on garbage) and pinned so the
    /// consequence is visible rather than discovered.
    void presentButUnreadableValueIsLockedNotAbsent_data() {
        QTest::addColumn<QString>("wire");
        QTest::addColumn<bool>("numeric");

        QTest::newRow("number-one")   << QStringLiteral("1") << true;
        QTest::newRow("number-zero")  << QStringLiteral("0") << true;
        QTest::newRow("unrecognised") << QStringLiteral("yes") << false;
        QTest::newRow("empty-string") << QString() << false;
        QTest::newRow("whitespace")   << QStringLiteral(" true") << false;
    }

    void presentButUnreadableValueIsLockedNotAbsent() {
        QFETCH(QString, wire);
        QFETCH(bool, numeric);

        QJsonObject obj{{"id", "garbage"}};
        if (numeric) {
            obj.insert(QStringLiteral("is_download_unlocked"), wire.toInt());
        } else {
            obj.insert(QStringLiteral("is_download_unlocked"), wire);
        }

        auto parsed = ClipParser::parseClip(obj);
        QVERIFY(parsed.has_value());
        QVERIFY2(parsed->is_download_unlocked.has_value(),
                 "a present-but-unreadable value was dropped to absent, which "
                 "would let it through the gate on a technicality");
        QCOMPARE(*parsed->is_download_unlocked, false);
    }

    void disabledReasonIsCarriedVerbatimAndDefaultsEmpty() {
        // Absent -> empty, so "unlocked" and "locked with no stated reason" are
        // told apart by is_download_unlocked, never by this string.
        auto bare = ClipParser::parseClip(QJsonObject{{"id", "reason-absent"}});
        QVERIFY(bare.has_value());
        QVERIFY(bare->download_disabled_reason.empty());

        // The one value actually observed in the corpus.
        auto contest = ClipParser::parseClip(QJsonObject{
            {"id", "reason-remix-contest"},
            {"download_disabled_reason", "remix_contest"},
        });
        QVERIFY(contest.has_value());
        QCOMPARE(contest->download_disabled_reason, std::string("remix_contest"));

        // Verbatim, with no normalisation: this string is interpolated into a
        // user-facing sentence, so any folding would misreport what the server
        // said.
        auto punctuated = ClipParser::parseClip(QJsonObject{
            {"id", "reason-punctuated"},
            {"download_disabled_reason", "Remix_Contest (Weekly; #3)"},
        });
        QVERIFY(punctuated.has_value());
        QCOMPARE(punctuated->download_disabled_reason,
                 std::string("Remix_Contest (Weekly; #3)"));

        // Empty string is empty, not "present but blank" — there is one string
        // type here, so the disambiguator is the flag alone.
        auto blank = ClipParser::parseClip(QJsonObject{
            {"id", "reason-blank"},
            {"download_disabled_reason", ""},
        });
        QVERIFY(blank.has_value());
        QVERIFY(blank->download_disabled_reason.empty());

        // Wrong type on the reason degrades to empty rather than throwing, in
        // keeping with ClipParser's tolerance contract.
        auto wrongType = ClipParser::parseClip(QJsonObject{
            {"id", "reason-number"},
            {"download_disabled_reason", 7},
        });
        QVERIFY(wrongType.has_value());
        QVERIFY(wrongType->download_disabled_reason.empty());
    }

    /// The entitlement read must not change what an early-rejected clip does.
    /// A payload carrying a denial and no id still fails on the id, which is the
    /// whole reason the field can never be read for it.
    void aClipWithoutAnIdIsStillAParseError() {
        QVERIFY(!ClipParser::parseClip(QJsonObject{}).has_value());

        auto noIdButDenied = ClipParser::parseClip(QJsonObject{
            {"title", "no id"},
            {"is_download_unlocked", false},
            {"download_disabled_reason", "remix_contest"},
        });
        QVERIFY2(!noIdButDenied.has_value(),
                 "a clip with no id became parseable because it also carried an "
                 "entitlement field");
    }

    /// Adding two reads must not disturb the other ~30. Spot-checks a
    /// deliberately broad object: a mis-placed brace in the parser would
    /// silently overwrite an unrelated field, and only an assertion across
    /// several of them catches that.
    ///
    /// Built from QJsonObject initialisers rather than a raw string literal, and
    /// that is load-bearing rather than taste. **moc cannot lex a raw string
    /// literal containing `//`.** Its preprocessor strips `//...` to end-of-line
    /// without knowing it is inside a string, so the raw string never closes,
    /// and moc then fails *silently* — one "note: No relevant classes found. No
    /// output generated." on stderr, an empty `test_DownloadEntitlement.moc`, and
    /// a build failure at `#include "test_DownloadEntitlement.moc` that names the
    /// moc include and never the offending line. Measured on moc 6.11.1: a
    /// one-line `R"({"u": "https://x.test/y"})"` and a multi-line one both
    /// produce a 0-byte .moc, while the same bytes in a normal escaped string
    /// literal are fine (which is why test_ClipParser's `kFullClipJson` is a
    /// regular string). Three of this file's URLs contain `//`, so the whole
    /// fixture is built programmatically.
    void entitlementParsingDoesNotDisturbExistingFields() {
        QJsonArray mediaUrls;
        mediaUrls.append(QJsonObject{
            {"url", "https://audiopipe.suno.ai/stream.m4a"},
            {"content_type", "m4a-opus"},
            {"delivery", "progressive"},
        });

        const QJsonObject obj{
            {"id", "everything"},
            {"title", "Neon Arch"},
            {"status", "complete"},
            {"audio_url", "https://studio-api.prod.suno.com/api/forbidden"},
            {"play_count", 12345},
            {"upvote_count", 678},
            {"batch_index", 2},
            {"allow_comments", true},
            {"is_verified", false},
            {"has_hook", true},
            {"is_persona_root", false},
            {"is_liked", true},
            {"is_trashed", false},
            {"is_public", true},
            {"model_name", "chirp-v4"},
            {"image_url", "https://cdn2.suno.ai/image.jpeg"},
            {"media_urls", mediaUrls},
            {"metadata", QJsonObject{{"tags", "synthwave"},
                                     {"prompt", "arch linux"},
                                     {"lyrics", "[Verse]"}}},
            {"is_download_unlocked", false},
            {"download_disabled_reason", "remix_contest"},
        };

        auto parsed = ClipParser::parseClip(obj);

        QVERIFY(parsed.has_value());
        const SunoClip& clip = *parsed;

        QCOMPARE(clip.id, std::string("everything"));
        QCOMPARE(clip.title, std::string("Neon Arch"));
        QCOMPARE(clip.status, std::string("complete"));
        QCOMPARE(clip.model_name, std::string("chirp-v4"));
        QCOMPARE(clip.audio_url, std::string("https://studio-api.prod.suno.com/api/forbidden"));
        QCOMPARE(clip.image_url, std::string("https://cdn2.suno.ai/image.jpeg"));
        QCOMPARE(clip.play_count, static_cast<i64>(12345));
        QCOMPARE(clip.upvote_count, static_cast<i64>(678));
        QCOMPARE(clip.batch_index, static_cast<i64>(2));
        QVERIFY(clip.allow_comments);
        QVERIFY(!clip.is_verified);
        QVERIFY(clip.has_hook);
        QVERIFY(!clip.is_persona_root);
        QVERIFY(clip.is_liked);
        QVERIFY(!clip.is_trashed);
        QVERIFY(clip.is_public);
        QCOMPARE(clip.metadata.tags, std::string("synthwave"));
        QCOMPARE(clip.metadata.prompt, std::string("arch linux"));
        QCOMPARE(clip.metadata.lyrics, std::string("[Verse]"));
        QVERIFY(clip.media_urls.size() == 1);
        QCOMPARE(clip.media_urls[0].content_type, std::string("m4a-opus"));
        QCOMPARE(clip.media_urls[0].delivery, std::string("progressive"));

        // ...and the new fields still landed, so the spot-check did not pass by
        // short-circuiting the whole parse.
        QVERIFY(clip.is_download_unlocked.has_value());
        QCOMPARE(*clip.is_download_unlocked, false);
        QCOMPARE(clip.download_disabled_reason, std::string("remix_contest"));
    }

    /// The production path is `feed/v3`, not a hand-built object, so the four
    /// captured shapes are pushed through the envelope parser together. This is
    /// where the distribution from the corpus is reproduced: 4 locked-with-
    /// reason, 32 locked-without, the remainder carrying no field at all.
    void theFieldsSurviveAFeedEnvelope() {
        QJsonArray clips{
            objFrom(R"({"id": "a", "status": "complete", "is_download_unlocked": false,
                         "download_disabled_reason": "remix_contest"})"),
            objFrom(R"({"id": "b", "status": "complete", "is_download_unlocked": false})"),
            objFrom(R"({"id": "c", "status": "complete", "is_download_unlocked": true})"),
            objFrom(R"({"id": "d", "status": "complete"})"),
        };

        auto page = ClipParser::parseFeedEnvelope(QJsonObject{
            {"clips", clips},
            {"has_more", false},
        });
        QVERIFY(page.has_value());
        QCOMPARE(page->clips.size(), 4);

        QVERIFY(page->clips[0].is_download_unlocked.has_value());
        QCOMPARE(*page->clips[0].is_download_unlocked, false);
        QCOMPARE(page->clips[0].download_disabled_reason, std::string("remix_contest"));

        QVERIFY(page->clips[1].is_download_unlocked.has_value());
        QCOMPARE(*page->clips[1].is_download_unlocked, false);
        QVERIFY(page->clips[1].download_disabled_reason.empty());

        QVERIFY(page->clips[2].is_download_unlocked.has_value());
        QCOMPARE(*page->clips[2].is_download_unlocked, true);

        QVERIFY2(!page->clips[3].is_download_unlocked.has_value(),
                 "a clip with no entitlement field came back denied");

        // The envelope's own fields survive the added reads.
        QVERIFY(!page->hasMore);
        QVERIFY(page->nextCursor.isEmpty());
    }

    // ── 2. the gate, through the public download() ────────────────────────────

    /// Branch one: the server named a reason, so it is quoted and nothing is
    /// speculated. `remix_contest` is the only value in the corpus.
    void lockedClipWithReasonQuotesItVerbatim() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());
        QSignalSpy saved(&fixture.downloader(), &SunoDownloader::fileSaved);

        auto clip = serverLocked(fetchableClip("locked-remix", "Locked Remix"), "remix_contest");
        auto result = fixture.downloader().download(clip);
        QVERIFY(!result.has_value());
        QCOMPARE(result.error(), kRefusalWithReason.arg(QStringLiteral("remix_contest")));
        // Exact equality above already excludes the speculative sentence: when
        // the server DOES say why, the client does not guess.
        QVERIFY(!result.error().contains(QStringLiteral("monthly")));

        // The whole point of consulting the server first: nothing is fetched.
        QVERIFY2(fixture.server().requests.empty(),
                 "the gate refused but a request was still issued");
        QVERIFY(fixture.server().replies.empty());
        QCOMPARE(saved.count(), 0);

        // Nothing local either — no destination file, no scratch file.
        QVERIFY(!QFile::exists(fixture.dir() + QStringLiteral("/Locked Remix.mp3")));
        QVERIFY(!QFile::exists(fixture.dir() + QStringLiteral("/Locked Remix.mp3.part")));
    }

    /// Branch two: locked, no stated reason. 32 of the 40 captured clips are in
    /// this state, so it is the common case and must not read as a broken
    /// message.
    void lockedClipWithNoReasonGetsADifferentSentence() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());

        auto clip = serverLocked(fetchableClip("locked-quiet", "Locked Quiet"));
        auto result = fixture.downloader().download(clip);
        QVERIFY(!result.has_value());
        QCOMPARE(result.error(), kRefusalNoReason);

        // Two different shapes of refusal, two different sentences.
        QVERIFY2(result.error() != kRefusalWithReason.arg(QStringLiteral("remix_contest")),
                 "a reasonless refusal was given the reason-carrying sentence");
        QVERIFY(!result.error().contains(QStringLiteral("()")));

        QVERIFY(fixture.server().requests.empty());
    }

    /// The hedge is the load-bearing part of that sentence, and the quota behind
    /// it is `[LEAD]`-grade: a metered per-plan allowance is documented by a
    /// third party, never by a capture. Asserting it to a user as the
    /// explanation would be an AGENTS.md §1 violation wearing a sentence, so the
    /// certainty words are asserted absent.
    void lockedClipWithNoReasonOffersTheAllowanceAsPossibilityNotFact() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());

        auto result = fixture.downloader().download(
            serverLocked(fetchableClip("locked-hedged", "Locked Hedged")));
        QVERIFY(!result.has_value());
        const QString sentence = result.error();

        QVERIFY2(sentence.contains(QStringLiteral("can be")), qPrintable(sentence));
        for (const QString& overclaim : {QStringLiteral("quota"),
                                         QStringLiteral("limit"),
                                         QStringLiteral("exhaust"),
                                         QStringLiteral("used up"),
                                         QStringLiteral("because")}) {
            QVERIFY2(!sentence.contains(overclaim, Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("refusal states the %1 allowance as fact: %2")
                                    .arg(overclaim, sentence)));
        }
    }

    /// THE regression guard. Absent must not refuse.
    ///
    /// Two halves, and the second is the one that carries the meaning: an absent
    /// clip that the format gate rejects produces the *format* sentence, which
    /// can only come from `noUsableMediaMessage()` — a function that runs
    /// strictly after the entitlement gate. Asserting "not refused" alone would
    /// pass just as well if the clip had been refused for an unrelated reason.
    void absentEntitlementIsNotARefusal() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());
        QSignalSpy saved(&fixture.downloader(), &SunoDownloader::fileSaved);

        // (a) absent + media this client would fetch -> a real download happens.
        SunoClip legacy = fetchableClip("legacy-absent", "Legacy Absent");
        QVERIFY(!legacy.is_download_unlocked.has_value());
        auto allowed = fixture.downloader().download(legacy);
        if (!allowed) {
            QFAIL(qPrintable(QStringLiteral("a clip with no entitlement field was refused: %1")
                                 .arg(allowed.error())));
        }
        QCOMPARE(static_cast<int>(fixture.server().requests.size()), 1);
        QVERIFY(fixture.succeedTransfer(QStringLiteral("legacy-absent")));
        QCOMPARE(saved.count(), 1);

        // (b) absent + the captured m4a-opus media -> refused by the FORMAT gate,
        //     with the format gate's own exact sentence. Proof of ordering.
        auto reachedFormatGate = fixture.downloader().download(opusOnlyClip("legacy-opus"));
        QVERIFY(!reachedFormatGate.has_value());
        QCOMPARE(reachedFormatGate.error(), kRefusalUnfetchableMedia);
        QVERIFY2(!reachedFormatGate.error().contains(QStringLiteral("does not allow")),
                 "an absent entitlement field was refused by the entitlement gate");
        QCOMPARE(static_cast<int>(fixture.server().requests.size()), 1);  // still just the one
    }

    /// An explicit grant proceeds, with the same absence of a network cost.
    void unlockedClipProceeds() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());
        QSignalSpy saved(&fixture.downloader(), &SunoDownloader::fileSaved);

        SunoClip granted = fetchableClip("granted", "Granted");
        granted.is_download_unlocked = true;
        auto result = fixture.downloader().download(granted);
        QVERIFY(result.has_value());
        QCOMPARE(result->clipId, std::string("granted"));
        QVERIFY(!result->reusedOnDisk);
        QCOMPARE(static_cast<int>(fixture.server().requests.size()), 1);
        QVERIFY(fixture.succeedTransfer(QStringLiteral("granted")));
        QCOMPARE(saved.count(), 1);
    }

    /// A reason alone is NOT a denial. The two fields are independent, and
    /// `download_disabled_reason` documents "when the server says why" — so a
    /// payload carrying a stale reason alongside an absent flag must proceed.
    /// This guards the tempting simplification "refuse whenever a reason is
    /// present", which would refuse every legacy clip that happened to be saved
    /// with a reason field.
    void aReasonAloneDoesNotRefuse() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());

        SunoClip staleReason = fetchableClip("stale-reason", "Stale Reason");
        staleReason.download_disabled_reason = "remix_contest";
        QVERIFY(!staleReason.is_download_unlocked.has_value());

        auto result = fixture.downloader().download(staleReason);
        if (!result) {
            QFAIL(qPrintable(QStringLiteral("a reason without a denial refused the clip: %1")
                                 .arg(result.error())));
        }
        QCOMPARE(static_cast<int>(fixture.server().requests.size()), 1);
    }

    /// Ordering, part 1: a locked clip whose media the format gate would have
    /// rejected anyway must produce the ENTITLEMENT sentence. Before this gate
    /// existed, that clip reported a local-filter failure for a server refusal —
    /// the exact misdiagnosis the change exists to remove, so it is pinned from
    /// both ends: the entitlement sentence present, the format sentence absent.
    void entitlementOutranksTheFormatReason() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());

        auto result = fixture.downloader().download(
            serverLocked(opusOnlyClip("locked-opus"), "remix_contest"));
        QVERIFY(!result.has_value());
        QCOMPARE(result.error(), kRefusalWithReason.arg(QStringLiteral("remix_contest")));
        QVERIFY2(!result.error().contains(QStringLiteral("will not fetch")),
                 "the format gate spoke first, so a server refusal was reported "
                 "as a local filter failure");
        QVERIFY(fixture.server().requests.empty());
    }

    /// Ordering, part 2: the same against the unfinished-status branch, which
    /// also lives inside selectDownloadUrl(). "Not finished yet" is the wrong
    /// diagnosis for a clip the server will never let this account save.
    void entitlementOutranksTheUnfinishedStatusReason() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());

        SunoClip clip = serverLocked(fetchableClip("locked-streaming", "Locked Streaming"));
        clip.status = "streaming";
        auto result = fixture.downloader().download(clip);
        QVERIFY(!result.has_value());
        QVERIFY2(!result.error().contains(QStringLiteral("not finished")),
                 "a server refusal was reported as an unfinished clip");
        QCOMPARE(result.error(), kRefusalNoReason);
        QVERIFY(fixture.server().requests.empty());
    }

    /// Ordering, part 3: the gate also precedes the already-on-disk shortcut, so
    /// a locked clip is refused rather than announced as saved.
    ///
    /// Recorded as an ordering consequence, not as an endorsement. The argument
    /// for the other answer is real (the user may hold a file they were once
    /// allowed to fetch, and telling them Suno forbids it is then a
    /// misdiagnosis), so this pins what the code does and leaves the judgement
    /// visible rather than burying it.
    void entitlementOutranksTheAlreadyOnDiskShortcut() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());
        QSignalSpy saved(&fixture.downloader(), &SunoDownloader::fileSaved);

        QFile onDisk(fixture.dir() + QStringLiteral("/Locked On Disk.mp3"));
        QVERIFY(onDisk.open(QIODevice::WriteOnly));
        onDisk.write("stale bytes");
        onDisk.close();

        SunoClip clip = serverLocked(fetchableClip("locked-on-disk", "Locked On Disk"),
                                     "remix_contest");
        auto result = fixture.downloader().download(clip);
        QVERIFY(!result.has_value());
        QCOMPARE(result.error(), kRefusalWithReason.arg(QStringLiteral("remix_contest")));
        QCOMPARE(saved.count(), 0);
        QVERIFY(fixture.server().requests.empty());
    }

    /// A refusal leaves no local trace either. `resolveDestPath` claims a
    /// filename for the life of the process, so a gate placed after it would
    /// burn a name on a clip that was never downloaded — and the burn is
    /// invisible until a *different* clip with the same title arrives and is
    /// disambiguated for no visible reason. So: refuse, then confirm the next
    /// clip to ask for that title still gets the plain name.
    void aRefusedClipClaimsNoDestination() {
        EntitlementConfigGuard config;
        Fixture fixture;
        QVERIFY(fixture.valid());
        QSignalSpy saved(&fixture.downloader(), &SunoDownloader::fileSaved);

        auto refused = fixture.downloader().download(
            serverLocked(fetchableClip("burn-attempt", "Shared Title"), "remix_contest"));
        QVERIFY(!refused.has_value());
        QCOMPARE(saved.count(), 0);

        // Same title, this time allowed. It must land on the unadorned name; a
        // claim by the refused clip would produce "Shared Title-burn-attempt.mp3".
        auto allowed = fixture.downloader().download(fetchableClip("allowed", "Shared Title"));
        QVERIFY(allowed.has_value());
        QVERIFY(fixture.succeedTransfer(QStringLiteral("allowed")));
        QCOMPARE(saved.count(), 1);
        QVERIFY2(saved.takeFirst().at(1).toString().endsWith(QStringLiteral("/Shared Title.mp3")),
                 "the refused clip claimed a destination the allowed clip then "
                 "had to be disambiguated away from");
    }

    // ── 3. messages must not overclaim ───────────────────────────────────────

    /// No sentence the downloader can return may name the internal mp3 filter or
    /// a host. Both are implementation facts a user has no way to know about,
    /// and naming them turns an explained refusal into a confusing one — the
    /// mistake the `noUsableMediaMessage` comment explicitly records having made.
    ///
    /// The sentences are produced by calling download() rather than copied here,
    /// so the sweep cannot pass because a copy here drifted from the real text.
    void noRefusalMessageLeaksTheInternalMp3FilterOrAHostName() {
        EntitlementConfigGuard config;
        QStringList sentences;

        const auto refusalFor = [](Fixture& fixture, SunoClip clip) -> QString {
            auto result = fixture.downloader().download(clip);
            if (result) {
                return {};  // accepted, so there is no sentence to inspect
            }
            return result.error();
        };

        {
            Fixture fixture;
            QVERIFY(fixture.valid());

            // Every distinct path through download()'s pre-transfer gates.
            sentences << refusalFor(fixture, serverLocked(
                                            fetchableClip("leak-locked", "Leak Locked"),
                                            "remix_contest"));
            sentences << refusalFor(fixture,
                                    serverLocked(fetchableClip("leak-quiet", "Leak Quiet")));
            sentences << refusalFor(fixture, opusOnlyClip("leak-opus"));

            SunoClip unfinished = fetchableClip("leak-streaming", "Leak Streaming");
            unfinished.status = "streaming";
            sentences << refusalFor(fixture, unfinished);

            sentences << refusalFor(fixture, SunoClip{});  // no id

            // Five refusals, zero bytes on disk and zero requests: the whole
            // scope of this block is the pre-transfer decision, so anything at
            // all in the download directory would mean a gate ran too late.
            QVERIFY2(QDir(fixture.dir()).isEmpty(), qPrintable(
                         QStringLiteral("refused clips left files behind in %1").arg(fixture.dir())));
            QVERIFY(fixture.server().requests.empty());
        }

        {
            // The WAV gate is a separate decision with its own sentence, and it
            // is the one message that legitimately names a format — the format
            // the user asked for, not the filter.
            CONFIG.suno().downloadFormat = SunoDownloadFormat::WAV;
            Fixture fixture;
            QVERIFY(fixture.valid());
            auto result = fixture.downloader().download(fetchableClip("leak-wav", "Leak Wav"));
            QVERIFY(!result.has_value());
            sentences << result.error();
        }

        QCOMPARE(sentences.size(), 6);
        for (const QString& sentence : sentences) {
            QVERIFY2(!sentence.isEmpty(), "a refusal returned no sentence at all");
            for (const QString& leak : {QStringLiteral("mp3"),
                                         QStringLiteral("audiopipe"),
                                         QStringLiteral("suno.ai"),
                                         QStringLiteral("suno.com"),
                                         QStringLiteral("content_type"),
                                         QStringLiteral("delivery"),
                                         QStringLiteral("media_urls")}) {
                QVERIFY2(!sentence.contains(leak, Qt::CaseInsensitive),
                         qPrintable(QStringLiteral("refusal leaks the internal \"%1\": %2")
                                        .arg(leak, sentence)));
            }
        }
    }
};

int runTestDownloadEntitlement(int argc, char** argv) {
    TestDownloadEntitlement t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_DownloadEntitlement.moc"