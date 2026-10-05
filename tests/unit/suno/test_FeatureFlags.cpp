// test_FeatureFlags.cpp — coverage for the gate resolver and the session-capability
// parser.
//
// ── Why this file exists at all ────────────────────────────────────────────────
// `src/suno/FeatureFlags.{hpp,cpp}` shipped on 2026-10-05 with **zero** test
// coverage. That is not a style complaint, it is the specific failure the backlog
// calls out: a security policy with no test is a policy that decays. The file it
// covers decides whether a product surface may be offered, and its step 2 (the
// evidence floor) is the one control standing between a `[LEAD]` route and the
// UI. A refactor that moved that check one line too late would have compiled, run
// green in every existing suite, and shipped a fail-open.
//
// The coverage is deliberately shaped around the *reasons*, not only the status
// enums. `GateStatus::ServerGated` alone is useless to a caller: the header's
// whole argument is that "disabled" is not an answer a user can act on, and the
// sentence is the product. A test that asserted only the enum would pass against
// a resolver that reported the correct verdict for the wrong cause.
//
// ── What these tests cannot do, recorded here so nobody assumes otherwise ──────
// **(a) A typo in a catalog flag name is undetectable from inside this file.** The
// flag-map fixtures are built *from* `featureCatalog()`, because the header is
// explicit that the flag map is DATA and the catalog is CODE and that a flag list
// baked anywhere in this tree is the bug the design exists to prevent (the 47-vs-57
// anonymous/authenticated gap, and `custom-model-ui` moving inside one observation
// window). So `CatalogFlagsAreCleanCsv` can prove a flag list is *well formed* —
// no stray space the tolerant splitter would silently absorb, no empty token, no
// duplicate — but it cannot prove "editing-stems" is the spelling Suno sends. Only
// a capture answers that. Anyone tempted to close the gap by pasting a 47-name list
// into a test should read the header's two-axes section first: they would be
// creating the second owner it refuses to have.
// **(b) The stored-switch half of "changes no state" is unobservable.** `setLocallyEnabled`
// returning `false` is observable, and so is the fact that the verdict reason does
// not claim the switch is on. Whether `locallyEnabled_[i]` was actually left
// untouched is *not*: the class exposes no `isLocallyEnabled()` accessor, and every
// gate where the enable is refused short-circuits at step 1 or step 2, which are
// both evaluated before step 5 ever reads the array. So the header's stated harm —
// "the switch would say `true` while the verdict said `EvidenceBlocked`" — is a
// real design concern about a *future* diagnostics accessor rather than an
// observable defect today. `ARefusedEnableLeavesTheVerdictReasonConsistent` pins
// the half that is observable, and says so.
//
// ── Style ─────────────────────────────────────────────────────────────────────
// 4-space indent, attached braces, left-pointing pointers, Qt includes before std
// (AGENTS.md §7). `toString` free functions return `const char*` precisely so Qt's
// QCOMPARE finds them by ADL (FeatureFlags.hpp:303-306), so every enum comparison
// below is spelled `QCOMPARE(verdict.status, GateStatus::Available)` rather than a
// cast to int.

#include <QtTest>

#include "suno/FeatureFlags.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace vc::suno;

namespace {

/// `FlagMapShape` has no `toString` in the header, unlike the three enums beside
/// it, so a bare `QCOMPARE(caps.flagMapShape, FlagMapShape::Present)` would print
/// an integer on failure and lose the row's meaning. This names it for the
/// failure output instead — and deliberately lives in this file's anonymous
/// namespace rather than in `vc::suno`, so the test never becomes a second owner
/// of a vocabulary the source may later grow its own name for.
[[nodiscard]] const char* shapeName(const FlagMapShape shape) noexcept {
    switch (shape) {
        case FlagMapShape::Absent:
            return "absent";
        case FlagMapShape::NotAnObject:
            return "not-an-object";
        case FlagMapShape::Present:
            return "present";
    }
    return "unknown";
}

/// Parse a JSON literal into a `QJsonObject`, failing the test rather than
/// proceeding if the *fixture* is malformed. Every envelope below is valid JSON:
/// the tolerance under test is the parser's reaction to wrong-typed values, not
/// to invalid JSON, and Qt rejects the latter at construction time.
QJsonObject envelope(const QByteArray& json) {
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        // `QTest::qFail` rather than the `QFAIL` macro: `QFAIL` expands to a
        // `return`, and this function returns a `QJsonObject`. The failure has to
        // be reported here, not turned into an empty envelope that the next
        // assertion would then pass against.
        QTest::qFail(
                qPrintable(
                        QStringLiteral("fixture is not valid JSON: %1").arg(error.errorString())),
                __FILE__, __LINE__);
    }
    return document.object();
}

/// A row's identity for a failure message. `QVERIFY2` takes a `const char*`, and
/// `FeatureDefinition::title` is a `std::string_view` into the catalog's static
/// storage — not guaranteed null-terminated, so it cannot be handed over as one.
/// `toString(FeatureGate)` is, which is why every assertion below names the gate
/// and lets the catalog carry the rest.
[[nodiscard]] const char* gateLabel(const FeatureDefinition& def) noexcept {
    return toString(def.gate);
}

/// `QJsonObject` has no `QTest::toString` overload, but the generic fallback in
/// `qtesttostring.h` streams through `QDebug`, so comparing a serialised pair is
/// readable on failure — and it compares the *bytes*, which is what "preserved
/// verbatim" actually claims.
QByteArray canonical(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

/// Every flag name any catalog row mentions, as an all-`true` flag map.
///
/// This is the strongest probe the file can make of the resolution order: no
/// server flag exists that any gate could ask for and not get, and no client
/// switch is left to try. Anything still not `Available` under it was refused by
/// step 1 or step 2 — a product/legal exclusion or an evidence floor — which is
/// exactly the claim `EveryPermanentlyBlockedGateIsNamedHere` makes.
QJsonObject everyCatalogFlagEnabled() {
    QJsonObject flags;
    for (const FeatureDefinition& def : featureCatalog()) {
        std::size_t start = 0;
        const std::string_view csv = def.serverFlags;
        while (start <= csv.size()) {
            const std::size_t comma = csv.find(',', start);
            const std::size_t end = comma == std::string_view::npos ? csv.size() : comma;
            const std::string_view token = csv.substr(start, end - start);
            if (!token.empty()) {
                flags.insert(QString::fromStdString(std::string(token)), true);
            }
            if (comma == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
    }
    return flags;
}

/// A resolver that has been handed every catalog flag and asked to enable every
/// gate. Nothing about the server or this build can be added to it.
GateResolver maximallyPermissiveResolver() {
    QJsonObject env;
    env.insert(QStringLiteral("flags"), everyCatalogFlagEnabled());

    GateResolver resolver;
    resolver.setCapabilities(parseSessionCapabilities(env));
    for (const FeatureDefinition& def : featureCatalog()) {
        // `[[nodiscard]]`, and the value is used rather than discarded: the count
        // of refused enables is itself an assertion (it must equal the number of
        // gates that are excluded or below the evidence floor).
        (void)resolver.setLocallyEnabled(def.gate, true);
    }
    return resolver;
}

/// A test-local mirror of the source's `catalogIsCompleteAndOrdered()` consteval
/// predicate, parameterised over the span it judges.
///
/// It exists to prove that `static_assert` is **not vacuous**. A predicate that
/// accepted everything would satisfy the real assertion exactly as cheerfully as a
/// correct one, and the difference stays invisible until a gate silently loses its
/// row — at which point that gate resolves to nothing rather than to a verdict.
/// `TheCompletenessCheckRejectsABrokenCatalog` runs it against three deliberately
/// damaged catalogs and requires a rejection from each.
[[nodiscard]] bool completeAndOrdered(const std::span<const FeatureDefinition> rows) {
    if (rows.size() != kFeatureGateCount) {
        return false;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (static_cast<std::size_t>(rows[i].gate) != i) {
            return false;
        }
    }
    return true;
}

/// Strict CSV split, with **no** whitespace tolerance — the deliberate opposite
/// of the source's `splitFlagList`. Comparing the two is what lets the catalog
/// test assert that the tolerant branches are not being reached by today's data,
/// rather than merely hoping.
[[nodiscard]] std::vector<std::string_view> splitStrict(const std::string_view csv) {
    std::vector<std::string_view> out;
    if (csv.empty()) {
        return out;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = csv.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? csv.size() : comma;
        out.push_back(csv.substr(start, end - start));
        if (comma == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

} // namespace

class TestFeatureFlags : public QObject {
    Q_OBJECT

private slots:
    // ── parseSessionCapabilities: the shape of the flag map ─────────────────

    void aWellFormedEnvelopeRecordsEveryFlagExactlyOnce() {
        const SessionCapabilities caps = parseSessionCapabilities(
                envelope(R"({"flags":{"studio":true,"v3_alpha":false,"playlists":true}})"));

        QCOMPARE(shapeName(caps.flagMapShape), "present");
        QCOMPARE(caps.flagCount(), 3);

        QVERIFY(caps.hasFlag("studio"));
        QVERIFY(caps.hasFlag("playlists"));
        QVERIFY(caps.flags.contains("studio"));
        QVERIFY(caps.flags.contains("playlists"));
        QCOMPARE(caps.flagsFalse.size(), std::size_t{1});

        // The raw map is the map, not a re-serialisation of what we understood:
        // a diagnostics panel reading it must see the `false` the sets split out.
        QVERIFY(caps.flagsRaw.value(QStringLiteral("v3_alpha")).isBool());
        QCOMPARE(caps.flagsRaw.value(QStringLiteral("v3_alpha")).toBool(), false);
    }

    void anExplicitFalseIsRecordedAsFalseAndIsNotEnabled() {
        // THE fail-open guard, and the reason the two sets exist separately.
        // Folding `false` into "present" would make `hasFlag("v3_alpha")` true and
        // *unlock* a surface the server had just switched off — a single value in
        // the map reversing a decision. Pin the split, not just the count.
        const SessionCapabilities caps =
                parseSessionCapabilities(envelope(R"({"flags":{"studio":true,"v3_alpha":false}})"));

        QVERIFY(!caps.hasFlag("v3_alpha"));
        QVERIFY(caps.flagsFalse.contains("v3_alpha"));
        QVERIFY(!caps.flags.contains("v3_alpha"));

        // Present-but-off is a third state, and the only query that reaches it.
        // It is what lets a diagnostic say "the server told us this exists and
        // turned it off" rather than "no such feature".
        QVERIFY(caps.hasFlagEntry("v3_alpha"));
        QVERIFY(caps.hasFlagEntry("studio"));
        QVERIFY(!caps.hasFlagEntry("no-such-flag"));

        // And the presence count still counts it exactly once.
        QCOMPARE(caps.flagCount(), 2);
    }

    void aNonBoolFlagIsTreatedAsEnabledAndDiagnosedRatherThanDropped() {
        // The one arguably fail-open judgement in the file, pinned so that changing
        // it has to be a decision rather than a drive-by. A non-bool is not an
        // explicit `false`, so it lands in `flags` (enabled) *and* in
        // `flagsNonBool` (diagnosed). The justification is that the server enforces
        // its own entitlements regardless, so the worst case is a visible control
        // the server refuses — not an access this client did not have.
        const SessionCapabilities caps = parseSessionCapabilities(
                envelope(R"({"flags":{"weird":"yes","nullish":null,"count":3,"ok":true}})"));

        QVERIFY(caps.hasFlag("weird"));
        QVERIFY(caps.hasFlag("nullish"));
        QVERIFY(caps.hasFlag("count"));
        QVERIFY(caps.hasFlag("ok"));

        QCOMPARE(caps.flagsNonBool.size(), std::size_t{3});
        QVERIFY(caps.flagsNonBool.contains("weird"));
        QVERIFY(caps.flagsNonBool.contains("nullish"));
        QVERIFY(caps.flagsNonBool.contains("count"));
        QVERIFY(!caps.flagsNonBool.contains("ok"));

        // `flagCount()` is deliberately the PRESENCE count, because that is the
        // number the corpus quotes — 47 on an anonymous prod read, 57 on an
        // authenticated one. A non-bool name lives in both `flags` and
        // `flagsNonBool`, so summing all three sets would double-count it; the
        // header states this, and here is the arithmetic that would be wrong.
        QCOMPARE(caps.flagCount(), 4);
        QCOMPARE(caps.flags.size() + caps.flagsFalse.size(), caps.flagCount());
    }

    void anEmptyFlagMapIsPresentAndAnAbsentKeyIsAbsent() {
        // "an account with zero flags" is a fact; "the server never mentioned
        // flags" is a different one. Collapsing them is what turns a dark UI into a
        // mis-parse, which is the whole reason `FlagMapShape` is three verdicts.
        const SessionCapabilities empty = parseSessionCapabilities(envelope(R"({"flags":{}})"));
        QCOMPARE(shapeName(empty.flagMapShape), "present");
        QCOMPARE(empty.flagCount(), 0);
        QVERIFY(!empty.hasFlag("studio"));

        const SessionCapabilities missing = parseSessionCapabilities(envelope(R"({"user":{}})"));
        QCOMPARE(shapeName(missing.flagMapShape), "absent");
        QCOMPARE(missing.flagCount(), 0);
        QVERIFY(missing.flagsRaw.isEmpty());

        // And the anonymous envelope shape from the corpus, which carries `user`
        // and `models` but no `flags` at all.
        const SessionCapabilities anonymous = parseSessionCapabilities(
                envelope(R"({"user":{"id":"x"},"models":[],"roles":{},"configs":null})"));
        QCOMPARE(shapeName(anonymous.flagMapShape), "absent");
        QVERIFY(!anonymous.genEndpoint.has_value());
    }

    void flagsIsFoundAtEveryToleratedDepth_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expectedFlag");

        // The three scopes `pickNested` walks, in order. This mirrors the walk
        // SunoAccountManager.cpp:45-75 already does for `user` rather than
        // inventing a third nesting policy — a session envelope that is flat for
        // `user` but wrapped for `flags` is precisely the case one shared walk
        // handles and two bespoke ones do not.
        QTest::newRow("flat") << QByteArray(R"({"flags":{"studio":true}})")
                              << QStringLiteral("studio");
        QTest::newRow("under-session") << QByteArray(R"({"session":{"flags":{"studio":true}}})")
                                       << QStringLiteral("studio");
        QTest::newRow("under-data")
                << QByteArray(R"({"data":{"flags":{"studio":true}}})") << QStringLiteral("studio");
    }

    void flagsIsFoundAtEveryToleratedDepth() {
        QFETCH(QByteArray, json);
        QFETCH(QString, expectedFlag);

        const SessionCapabilities caps = parseSessionCapabilities(envelope(json));
        QCOMPARE(shapeName(caps.flagMapShape), "present");
        QVERIFY(caps.hasFlag(expectedFlag.toStdString()));
        QCOMPARE(caps.flagCount(), 1);
        // The map is captured wherever it was found, so a diagnostics panel does
        // not have to re-walk the nesting to show it. `flagsRaw` is the map, not a
        // projection of what we understood -- so it carries the same keys, and only
        // those.
        QCOMPARE(caps.flagsRaw.size(), 1);
        QVERIFY(caps.flagsRaw.contains(expectedFlag));
    }

    void theFlatMapOutranksNestedOnesAndSessionOutranksData() {
        // Scope precedence is a decision, not an accident of `pickNested`'s array
        // order, and it matters when the scopes disagree — which a server bug
        // could produce. Outer first, then `session`, then `data`.
        const SessionCapabilities both = parseSessionCapabilities(
                envelope(R"({"flags":{"outer":true},"session":{"flags":{"inner":true}}})"));
        QVERIFY(both.hasFlag("outer"));
        QVERIFY(!both.hasFlag("inner"));
        QCOMPARE(both.flagCount(), 1);

        const SessionCapabilities twoNested = parseSessionCapabilities(
                envelope(R"({"session":{"flags":{"s":true}},"data":{"flags":{"d":true}}})"));
        QVERIFY(twoNested.hasFlag("s"));
        QVERIFY(!twoNested.hasFlag("d"));
    }

    void aWrongTypedFlagsValueIsRecordedAndStopsTheWalk_data() {
        QTest::addColumn<QByteArray>("json");

        QTest::newRow("array") << QByteArray(R"({"flags":["a","b"]})");
        QTest::newRow("string") << QByteArray(R"({"flags":"nope"})");
        QTest::newRow("bool") << QByteArray(R"({"flags":true})");
        QTest::newRow("number") << QByteArray(R"({"flags":7})");
        // The case that pins the *stop*: a flat wrong-typed `flags` alongside a
        // perfectly good nested one. The walk returns NotAnObject at the first
        // wrong-typed value rather than searching on, which is the honest
        // "mis-parse worth saying out loud" answer instead of a lucky recovery
        // that would report a map the server never sent at that level.
        QTest::newRow("flat-wrong-type-stops-the-walk")
                << QByteArray(R"({"flags":"nope","session":{"flags":{"inner":true}}})");
    }

    void aWrongTypedFlagsValueIsRecordedAndStopsTheWalk() {
        QFETCH(QByteArray, json);

        const SessionCapabilities caps = parseSessionCapabilities(envelope(json));
        // Tolerated on the wire, reported rather than guessed at: `Absent` means an
        // account with no flags, `NotAnObject` means we could not read what came.
        QCOMPARE(shapeName(caps.flagMapShape), "not-an-object");
        QCOMPARE(caps.flagCount(), 0);
        QVERIFY(!caps.hasFlag("inner"));
        QVERIFY(caps.flagsRaw.isEmpty());

        // Nothing is invented, and the envelope is not thrown away on the way past.
        QVERIFY(!caps.genEndpoint.has_value());
        QCOMPARE(canonical(caps.raw), canonical(envelope(json)));
    }

    void aJsonNullFlagsValueIsTreatedAsEmptyAndTheWalkContinues() {
        // The observed anonymous envelope carries `configs: null` and
        // `experiments: null`, so a JSON null must not be read as a type error --
        // that would stop the walk and make a nested map unreachable. Verified
        // behaviour: the null scope is skipped and the walk finds the next one.
        const SessionCapabilities caps = parseSessionCapabilities(
                envelope(R"({"flags":null,"session":{"flags":{"inner":true}}})"));

        QCOMPARE(shapeName(caps.flagMapShape), "present");
        QVERIFY(caps.hasFlag("inner"));
        QCOMPARE(caps.flagCount(), 1);
    }

    // ── parseSessionCapabilities: configs["gen-endpoint"] ───────────────────

    void configsGenEndpointIsCapturedWhenItIsAString_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expected");

        // `configs` is null on every anonymous read and populates only when
        // authenticated, so the presence of this one value is itself a signal.
        QTest::newRow("flat") << QByteArray(
                                         R"({"configs":{"gen-endpoint":"/api/generate/v2-web/"}})")
                              << QStringLiteral("/api/generate/v2-web/");
        QTest::newRow("under-session")
                << QByteArray(R"({"session":{"configs":{"gen-endpoint":"/api/generate/v3/"}}})")
                << QStringLiteral("/api/generate/v3/");
    }

    void configsGenEndpointIsCapturedWhenItIsAString() {
        QFETCH(QByteArray, json);
        QFETCH(QString, expected);

        const SessionCapabilities caps = parseSessionCapabilities(envelope(json));
        QVERIFY(caps.genEndpoint.has_value());
        QCOMPARE(QString::fromStdString(*caps.genEndpoint), expected);
    }

    void configsGenEndpointIsNeverInvented_data() {
        QTest::addColumn<QByteArray>("json");

        // The load-bearing half. This is the server telling the client which
        // generate route to call, and its absence must never be guessed into a
        // default route — that is exactly how a constructed path gets wired by
        // accident. Every row here must leave `genEndpoint` empty.
        QTest::newRow("no-configs") << QByteArray(R"({"flags":{}})");
        QTest::newRow("configs-null") << QByteArray(R"({"configs":null})");
        QTest::newRow("empty-string") << QByteArray(R"({"configs":{"gen-endpoint":""}})");
        QTest::newRow("json-null") << QByteArray(R"({"configs":{"gen-endpoint":null}})");
        QTest::newRow("number") << QByteArray(R"({"configs":{"gen-endpoint":42}})");
        QTest::newRow("object") << QByteArray(R"({"configs":{"gen-endpoint":{"a":1}}})");
        QTest::newRow("configs-array") << QByteArray(R"({"configs":[1]})");
        // Same stop-the-walk rule as `flags`, applied to the key that decides
        // which route to call: a wrong-typed `configs` does not fall through to a
        // nested good one.
        QTest::newRow("wrong-typed-stops-the-walk")
                << QByteArray(R"({"configs":"nope","session":{"configs":{"gen-endpoint":"/x/"}}})");
    }

    void configsGenEndpointIsNeverInvented() {
        QFETCH(QByteArray, json);

        const SessionCapabilities caps = parseSessionCapabilities(envelope(json));
        QVERIFY(!caps.genEndpoint.has_value());
    }

    // ── parseSessionCapabilities: verbatim preservation ─────────────────────

    void everyOtherKeyIsPreservedVerbatim() {
        // `roles`, `statsig_custom_properties`, `experiments` and
        // `data_sharing_consent` are preserved and **not interpreted**. Reading a
        // consent record is not the same act as writing one, which is why the read
        // is fine and `PrivacyConsentWrites` is excluded for the write.
        const QJsonObject input = envelope(
                R"({"flags":{"a":true},"roles":{"admin":true},)"
                R"("statsig_custom_properties":{"custom":{"user_plan_key":"pro"}},)"
                R"("experiments":{"e1":{"variant":2}},"data_sharing_consent":{"granted":false}})");

        const SessionCapabilities caps = parseSessionCapabilities(input);

        QCOMPARE(canonical(caps.roles), canonical(input.value(QStringLiteral("roles")).toObject()));
        QCOMPARE(canonical(caps.statsigCustom),
                 canonical(input.value(QStringLiteral("statsig_custom_properties")).toObject()));
        QCOMPARE(canonical(caps.experiments),
                 canonical(input.value(QStringLiteral("experiments")).toObject()));
        QCOMPARE(canonical(caps.dataSharingConsent),
                 canonical(input.value(QStringLiteral("data_sharing_consent")).toObject()));

        // And the whole envelope, nesting included. The parser is total, so the
        // honest answer to anything it cannot understand is "here is what arrived",
        // never a reconstruction.
        QCOMPARE(canonical(caps.raw), canonical(input));
    }

    void aWrongTypedVerbatimKeyYieldsAnEmptyObjectRatherThanAGuess() {
        // "Preserve verbatim" and "record that this key had the wrong type" are
        // different contracts. For these four the deliberate reading is the
        // second's *consequence* rather than its record: a wrong-typed key becomes
        // an empty object, which is indistinguishable from absent. `flags` is the
        // one key whose shape changes the meaning of what we know, so it is the
        // one key whose shape is kept.
        const SessionCapabilities caps = parseSessionCapabilities(
                envelope(R"({"roles":5,"statsig_custom_properties":"x","experiments":[],)"
                         R"("data_sharing_consent":1})"));

        QVERIFY(caps.roles.isEmpty());
        QVERIFY(caps.statsigCustom.isEmpty());
        QVERIFY(caps.experiments.isEmpty());
        QVERIFY(caps.dataSharingConsent.isEmpty());
        // ...and the envelope still survives, which is the half that must not be
        // paid for with data loss.
        QVERIFY(!caps.raw.isEmpty());
    }

    void parsingIsTotalOnAnEnvelopeThatIsWrongAboutEverything() {
        // Every key wrong-typed at once, at all three depths. The contract is that
        // `parseSessionCapabilities` cannot fail (there is no `std::expected` in
        // the header and that is deliberate), so this must return a usable answer
        // rather than throw and rather than fabricate.
        const QByteArray json = R"({"flags":[[1,2],{"a":1}],)"
                                R"("roles":[[]],"statsig_custom_properties":false,)"
                                R"("experiments":0,"data_sharing_consent":"",)"
                                R"("configs":{"gen-endpoint":{"a":1}},"session":7,"data":[],)"
                                R"("user":null})";
        const QJsonObject input = envelope(json);

        const SessionCapabilities caps = parseSessionCapabilities(input);

        QCOMPARE(shapeName(caps.flagMapShape), "not-an-object");
        QCOMPARE(caps.flagCount(), 0);
        QVERIFY(caps.flags.empty());
        QVERIFY(caps.flagsFalse.empty());
        QVERIFY(caps.flagsNonBool.empty());
        QVERIFY(!caps.genEndpoint.has_value());
        QCOMPARE(canonical(caps.raw), canonical(input));

        // And it is a usable object: a resolver holding it must behave, not crash.
        GateResolver resolver;
        resolver.setCapabilities(caps);
        QCOMPARE(resolver.evaluate(FeatureGate::Library).status, GateStatus::LocallyDisabled);
        QCOMPARE(resolver.evaluate(FeatureGate::Playlists).status, GateStatus::ServerGated);
    }

    void theParsedEnvelopeIsIndependentOfTheCallersCopy() {
        // `raw` and `flagsRaw` are the caller's object, not a reconstruction, and
        // `QJsonObject` is implicitly shared. A caller that keeps its envelope and
        // mutates it after handing it over must not be able to reach in and change
        // what the resolver now believes -- that would make the diagnostic view and
        // the gating verdict read from two different maps.
        QJsonObject input = envelope(R"({"flags":{"a":true}})");
        const SessionCapabilities caps = parseSessionCapabilities(input);

        input.insert(QStringLiteral("flags"), QJsonObject{{QStringLiteral("a"), false}});
        input.insert(QStringLiteral("late"), 1);

        QVERIFY(caps.hasFlag("a"));
        QCOMPARE(caps.flagCount(), 1);
        QCOMPARE(caps.flagsRaw.value(QStringLiteral("a")).toBool(), true);
        QCOMPARE(canonical(caps.raw), canonical(envelope(R"({"flags":{"a":true}})")));
    }

    void twoResolversHandedTheSameCapabilitiesAreIndependent() {
        // `setCapabilities` takes its argument by value, so two resolvers cannot
        // end up sharing one another's switches. Worth pinning because the whole
        // fail-closed story is "everything is off until something turns it on", and
        // a shared switch array would break that for every second resolver.
        const SessionCapabilities shared =
                parseSessionCapabilities(envelope(R"({"flags":{"a":true}})"));

        GateResolver first;
        GateResolver second;
        first.setCapabilities(shared);
        second.setCapabilities(shared);

        QVERIFY(first.setLocallyEnabled(FeatureGate::Library, true));

        QVERIFY(first.isAvailable(FeatureGate::Library));
        QVERIFY(!second.isAvailable(FeatureGate::Library));

        // `capabilities()` hands back the resolver's own copy, read-only.
        QCOMPARE(first.capabilities().flagCount(), 1);
        QVERIFY(first.capabilities().hasFlag("a"));
    }

    // ── Step 1 of six: Excluded ─────────────────────────────────────────────

    void theTwoExcludedGatesStayExcludedRegardlessOfFlagsOrSwitches() {
        // Step 1 sits ahead of everything else, so this is pinned in the resolver
        // that has already been given *everything*: every catalog flag present and
        // enabled, every switch set. If the exclusion ever moved to the bottom of
        // the order, `MoneyMoving` and `LegalConsentWrite` would become Available
        // and this test is what says they must not.
        const GateResolver maximal = maximallyPermissiveResolver();

        const GateVerdict billing = maximal.evaluate(FeatureGate::BillingMutations);
        QCOMPARE(billing.status, GateStatus::Excluded);
        QCOMPARE(billing.reason,
                 QStringLiteral("Excluded by product policy: a third-party client must not be "
                                "able to cancel a subscription, change a plan, or apply a "
                                "coupon. That is a support and terms-of-service surface, and "
                                "the decision holds even if a capture later lands."));

        const GateVerdict consent = maximal.evaluate(FeatureGate::PrivacyConsentWrites);
        QCOMPARE(consent.status, GateStatus::Excluded);
        QCOMPARE(consent.reason,
                 QStringLiteral("Excluded by product policy: this route writes a privacy or "
                                "programme consent record on the user's behalf, which is a "
                                "legal-terms surface rather than a feature. Reading the "
                                "existing record is a different act and is not excluded."));

        // The verdict names the decision, not the consequence, and it names the
        // subject matter. "we decided not to" and "we could not" are different
        // sentences, and a user asking about a subscription deserves the first.
        QVERIFY(billing.reason.contains(QStringLiteral("product policy")));
        QVERIFY(billing.reason.contains(QStringLiteral("coupon")));
        QVERIFY(consent.reason.contains(QStringLiteral("consent")));
        QVERIFY(!billing.reason.contains(QStringLiteral("switched off in this build")));
    }

    void anExclusionOutranksTheEvidenceFloorItWouldAlsoFail() {
        // Both excluded rows are graded `Lead`, so *both* steps 1 and 2 would
        // refuse them. Reporting `EvidenceBlocked` would be true and useless: it
        // would tell a future maintainer that a capture would fix it, and the
        // header is explicit that this judgement "does not evaporate if a capture
        // ever lands". One assertion, one ordering fact.
        const GateResolver maximal = maximallyPermissiveResolver();
        for (const FeatureGate gate :
             {FeatureGate::BillingMutations, FeatureGate::PrivacyConsentWrites}) {
            const FeatureDefinition* def = findFeature(gate);
            QVERIFY(def != nullptr);
            QVERIFY(def->exclusion != ExclusionReason::None);
            QVERIFY(static_cast<int>(def->evidence) > static_cast<int>(hosts::Evidence::Captured));
            QCOMPARE(maximal.evaluate(gate).status, GateStatus::Excluded);
        }
    }

    void theSwitchRefusesToHoldForAnExcludedGate() {
        GateResolver resolver;
        QVERIFY(!resolver.setLocallyEnabled(FeatureGate::BillingMutations, true));
        QVERIFY(!resolver.setLocallyEnabled(FeatureGate::PrivacyConsentWrites, true));
        QCOMPARE(resolver.evaluate(FeatureGate::BillingMutations).status, GateStatus::Excluded);

        // The refusal changes nothing at all, so the verdict reason must still be
        // the exclusion sentence. A stored `true` would not be visible here -- both
        // steps 1 and 2 short-circuit before step 5 reads the switch -- but the
        // *reason* is the observable half, and a diagnostics panel reads the reason.
        QCOMPARE(resolver.evaluate(FeatureGate::BillingMutations).reason,
                 maximallyPermissiveResolver().evaluate(FeatureGate::BillingMutations).reason);
    }

    // ── Step 2 of six: the evidence floor ───────────────────────────────────

    void thePermanentlyBlockedGatesAreNamedHere_data() {
        QTest::addColumn<int>("gate");

        // These four are blocked today and **no** switch can change that, which is
        // the point: a captured flag on an uncaptured route is exactly the trap
        // AGENTS.md §1 exists to close. Named explicitly so that promoting one of
        // them -- which is legitimate, and would come with a capture -- has to
        // change this file deliberately rather than slip through.
        //
        // `RealtimePush` is the interesting one: its ROUTE is `[T1]` and its body
        // is modelled exactly, but the gate is graded on the *host* -- the corpus
        // sent zero requests to `main.realtime.ably.net`, so it is a response value
        // and no credential may go there.
        QTest::newRow("personas") << static_cast<int>(FeatureGate::Personas);
        QTest::newRow("realtime-push") << static_cast<int>(FeatureGate::RealtimePush);
        QTest::newRow("experimental-orchestrator")
                << static_cast<int>(FeatureGate::ExperimentalOrchestrator);
        QTest::newRow("account-deletion") << static_cast<int>(FeatureGate::AccountDeletion);
    }

    void thePermanentlyBlockedGatesAreNamedHere() {
        QFETCH(int, gate);
        const auto feature = static_cast<FeatureGate>(gate);

        // Even with its own flag present and its switch asked for.
        QJsonObject flags;
        const FeatureDefinition* def = findFeature(feature);
        QVERIFY(def != nullptr);
        for (const std::string_view flag : splitStrict(def->serverFlags)) {
            flags.insert(QString::fromStdString(std::string(flag)), true);
        }
        QJsonObject env;
        env.insert(QStringLiteral("flags"), flags);

        GateResolver resolver;
        resolver.setCapabilities(parseSessionCapabilities(env));
        // The switch is refused for every one of these four, and that is a real
        // assertion rather than a formality: `switchMayHold` consults the evidence
        // floor and the exclusion, so a below-floor gate cannot even record the
        // intent to use itself when the flag eventually lands. The half of "changes
        // no state" that is observable is the verdict below.
        QVERIFY(!resolver.setLocallyEnabled(feature, true));

        const GateVerdict verdict = resolver.evaluate(feature);
        QCOMPARE(verdict.status, GateStatus::EvidenceBlocked);

        // The sentence names the grade and says the block is unconditional, which
        // is the user-facing half of the control. A reason that only said "off"
        // would leave a reader hunting, which is the failure this file exists for.
        QVERIFY(verdict.reason.contains(QStringLiteral("Blocked on evidence")));
        QVERIFY(verdict.reason.contains(QString::fromStdString(std::string(def->title))));
        QVERIFY(verdict.reason.contains(QStringLiteral("can override that")));
        QVERIFY(!resolver.isAvailable(feature));
    }

    void theBlockedSetIsExactlyThoseFourAndNotTheTwoExcludedOnes() {
        // Derived from the resolver rather than from a re-implementation of the
        // evidence ranking, so this asserts the observable: given every catalog
        // flag and every switch, precisely these gates and no others refuse on
        // evidence. `BillingMutations` and `PrivacyConsentWrites` are also graded
        // `Lead` and are deliberately *absent* from this list, which is step 1
        // ahead of step 2 made visible.
        const GateResolver maximal = maximallyPermissiveResolver();

        QStringList blocked;
        QStringList excluded;
        for (const FeatureDefinition& def : featureCatalog()) {
            switch (maximal.evaluate(def.gate).status) {
                case GateStatus::EvidenceBlocked:
                    blocked.append(QString::fromLatin1(toString(def.gate)));
                    break;
                case GateStatus::Excluded:
                    excluded.append(QString::fromLatin1(toString(def.gate)));
                    break;
                default:
                    break;
            }
        }

        // In catalog order, not alphabetical: `featureCatalog()` is ordered and the
        // comparison is meant to catch a gate moving as much as a gate changing.
        QCOMPARE(blocked, QStringList({"personas", "realtime-push", "experimental-orchestrator",
                                       "account-deletion"}));
        QCOMPARE(excluded, QStringList({"billing-mutations", "privacy-consent-writes"}));
    }

    void aBlockedSentenceNamesTheGradeRatherThanJustSayingNo_data() {
        QTest::addColumn<int>("gate");
        QTest::addColumn<QString>("expectedReason");

        // The three distinct grades produce three distinct sentences, because the
        // remedy differs: a `[LEAD]` needs a capture, a response-value host needs a
        // request we have actually sent to it, and no grade is fixed by a flag.
        QTest::newRow("personas")
                << static_cast<int>(FeatureGate::Personas)
                << QStringLiteral("Blocked on evidence: \"Personas (voices)\" is [LEAD] (bundle "
                                  "string or reconstructed path, not a capture). No server flag, "
                                  "config, tier or client switch can override that (AGENTS.md "
                                  "section 1).");
        QTest::newRow("realtime-push")
                << static_cast<int>(FeatureGate::RealtimePush)
                << QStringLiteral("Blocked on evidence: \"Realtime push\" is a response value only "
                                  "(the host appears inside another host's response and was never "
                                  "requested by us). No server flag, config, tier or client switch "
                                  "can override that (AGENTS.md section 1).");
    }

    void aBlockedSentenceNamesTheGradeRatherThanJustSayingNo() {
        QFETCH(int, gate);
        QFETCH(QString, expectedReason);

        const GateResolver resolver;
        QCOMPARE(resolver.evaluate(static_cast<FeatureGate>(gate)).reason, expectedReason);
    }

    void aRefusedEnableLeavesTheVerdictReasonConsistent() {
        // `setLocallyEnabled` is `[[nodiscard]]` and refusing is not an error
        // condition to be logged past; it is the answer. Pinned for the gate whose
        // row exists only to be refused, with its flag present so the refusal is
        // unambiguously about the floor and not about the flag.
        QJsonObject env;
        env.insert(QStringLiteral("flags"), QJsonObject{{QStringLiteral("voices-ui"), true}});

        GateResolver resolver;
        resolver.setCapabilities(parseSessionCapabilities(env));
        QVERIFY(resolver.capabilities().hasFlag("voices-ui"));
        QVERIFY(!resolver.setLocallyEnabled(FeatureGate::Personas, true));

        const GateVerdict verdict = resolver.evaluate(FeatureGate::Personas);
        QCOMPARE(verdict.status, GateStatus::EvidenceBlocked);
        // Not "switched off in this build" -- that sentence is step 5's, and
        // reaching it would mean the evidence floor had been reordered behind the
        // switch.
        QVERIFY(!verdict.reason.contains(QStringLiteral("switched off in this build")));
        QVERIFY(verdict.reason.contains(QStringLiteral("Blocked on evidence")));
    }

    void turningASwitchOffIsAcceptedEvenWhereTurningItOnIsRefused() {
        // The refusal guard is on `enabled`, not on the gate. A disable must always
        // be accepted, or a build could not turn a surface off after some future
        // caller had turned it on -- and the fail-closed default would be
        // unreachable in the one direction that matters.
        GateResolver resolver;
        QVERIFY(!resolver.setLocallyEnabled(FeatureGate::Personas, true));
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Personas, false));
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::BillingMutations, false));
        QCOMPARE(resolver.evaluate(FeatureGate::Personas).status, GateStatus::EvidenceBlocked);
        QCOMPARE(resolver.evaluate(FeatureGate::BillingMutations).status, GateStatus::Excluded);
    }

    // ── Step 3 of six: tier ─────────────────────────────────────────────────

    void noCatalogRowCanCurrentlyReachTheTierStep() {
        // An honest negative, recorded rather than papered over. The tier check is
        // the third of six steps and today **nothing can reach it**: the catalog's
        // only tier-gated row (`AccountDeletion`, `minTier == Staging`) is also
        // graded `Lead`, so step 2 refuses it first. There is no way to inject a
        // synthetic catalog row through the public API, so the tier branch is
        // guarded by this invariant rather than by an assertion that reaches it.
        // If a row ever becomes tier-gated *and* adequately evidenced, this test
        // fails and that is the signal to add a case for the new branch.
        QStringList tierGated;
        for (const FeatureDefinition& def : featureCatalog()) {
            if (static_cast<int>(def.minTier) > static_cast<int>(hosts::Tier::Production)) {
                tierGated.append(QString::fromLatin1(toString(def.gate)));
                QVERIFY(static_cast<int>(def.evidence) >
                        static_cast<int>(hosts::Evidence::Captured));
            }
        }
        QCOMPARE(tierGated, QStringList({"account-deletion"}));
    }

    void evidenceOutranksTierSoAStagingOnlySurfaceIsStillBlocked() {
        // Step 3 checked before step 4 because a staging-only surface has no
        // production flag map to be satisfied by -- but it is still step 2 first.
        // Account deletion is the compliance-adjacent code path that wants a human
        // decision before it is even designed, so its verdict must stay the
        // evidence sentence even on the tier where its flag lives.
        hosts::TierPolicy staging;
        QVERIFY(staging.raiseTo(hosts::Tier::Staging));
        QCOMPARE(staging.activeTier(), hosts::Tier::Staging);

        GateResolver resolver;
        resolver.setTierPolicy(staging);

        const GateVerdict verdict = resolver.evaluate(FeatureGate::AccountDeletion);
        QCOMPARE(verdict.status, GateStatus::EvidenceBlocked);
        QVERIFY(verdict.reason.contains(QStringLiteral("Blocked on evidence")));
        QVERIFY(!verdict.reason.contains(QStringLiteral("Not on this tier")));
    }

    void raisingTheTierIsAFloorForSurfacesAndNotAnExactMatch() {
        // Deliberate contrast with `hosts::TierPolicy::mayReceiveCredential`, where
        // matching is exact and raising the tier *narrows* the reachable host set.
        // A product-surface gate is the opposite shape: a staging build must not
        // lose the production surfaces, because a surface's `minTier` says where it
        // first exists, not which tier is the only one that has it.
        hosts::TierPolicy staging;
        QVERIFY(staging.raiseTo(hosts::Tier::Staging));

        QJsonObject env;
        env.insert(QStringLiteral("flags"), QJsonObject{{QStringLiteral("editing-stems"), true},
                                                        {QStringLiteral("generative-stems"), true},
                                                        {QStringLiteral("stem-dryer"), true},
                                                        {QStringLiteral("crop-remove"), true}});

        GateResolver resolver;
        resolver.setTierPolicy(staging);
        resolver.setCapabilities(parseSessionCapabilities(env));
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Stems, true));

        QCOMPARE(resolver.evaluate(FeatureGate::Stems).status, GateStatus::Available);
        QVERIFY(!resolver.evaluate(FeatureGate::Stems)
                         .reason.contains(QStringLiteral("Not on this tier")));
    }

    void theDefaultResolverAdoptsTheFailClosedProductionTier() {
        // Until `setTierPolicy` is called the resolver holds a default-constructed
        // `TierPolicy`, and the whole fail-closed story rests on what that default
        // is. Stated as an assertion rather than left implicit, because "the default
        // is the safe one" is the claim -- and because `TierPolicy` has no
        // constructor taking a tier, so there is no call site that can enable
        // staging by forgetting that it should not have.
        QCOMPARE(hosts::TierPolicy{}.activeTier(), hosts::Tier::Production);

        GateResolver resolver;
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Library, true));
        const GateVerdict verdict = resolver.evaluate(FeatureGate::Library);
        QCOMPARE(verdict.status, GateStatus::Available);
        // The tier step must not fire on a Production surface at Production.
        QVERIFY(!verdict.reason.contains(QStringLiteral("Not on this tier")));
    }

    // ── Step 4 of six: server flags ─────────────────────────────────────────

    void aDefaultResolverGatesOnTheServerFlagRatherThanAssumingIt() {
        // The signed-out reading, and the reason it is safe to construct a resolver
        // at all: an account with no flag map is not an account with every
        // feature. A resolver that defaulted these to Available would light up the
        // whole UI for an account we know nothing about.
        const GateResolver resolver;

        const GateVerdict playlists = resolver.evaluate(FeatureGate::Playlists);
        QCOMPARE(playlists.status, GateStatus::ServerGated);
        QVERIFY(!resolver.isAvailable(FeatureGate::Playlists));

        // Enabling the switch does not help, because step 4 precedes step 5: a
        // client-side switch cannot manufacture a server flag.
        GateResolver switched;
        QVERIFY(switched.setLocallyEnabled(FeatureGate::Playlists, true));
        QCOMPARE(switched.evaluate(FeatureGate::Playlists).status, GateStatus::ServerGated);
        QVERIFY(!switched.isAvailable(FeatureGate::Playlists));
    }

    void noGateIsAvailableToADefaultResolver() {
        // The whole fail-closed default in one assertion, over every row: the switch
        // table starts all-off, and the evidence floor and the two exclusions hold
        // regardless. This is the claim that replaces a hardcoded `false` in each
        // caller, so it is worth stating once for the entire catalog.
        const GateResolver resolver;
        for (const FeatureDefinition& def : featureCatalog()) {
            const GateVerdict verdict = resolver.evaluate(def.gate);
            QVERIFY2(verdict.status != GateStatus::Available,
                     qPrintable(QStringLiteral("%1 resolved Available on a bare resolver")
                                        .arg(QString::fromLatin1(toString(def.gate)))));
            // A verdict always carries a sentence. "Disabled" is not an answer a
            // user can act on, and an empty one would be worse.
            QVERIFY(!verdict.reason.isEmpty());
            QCOMPARE(verdict.gate, def.gate);
        }
    }

    void anAbsentFlagAndAnExplicitlyFalseOneAreDifferentAnswers() {
        // One is an account that lacks the feature; the other is a feature the
        // server switched off for this account. Same status, different sentence,
        // different remedy -- and a diagnostics panel that collapsed them would be
        // guessing.
        const QJsonObject absent = envelope(R"({"flags":{}})");
        const QJsonObject switchedOff =
                envelope(R"({"flags":{"playlists":false,"playlist-condition":true}})");

        GateResolver withoutIt;
        withoutIt.setCapabilities(parseSessionCapabilities(absent));
        GateResolver withIt;
        withIt.setCapabilities(parseSessionCapabilities(switchedOff));

        const GateVerdict missing = withoutIt.evaluate(FeatureGate::Playlists);
        const GateVerdict off = withIt.evaluate(FeatureGate::Playlists);

        QCOMPARE(missing.status, GateStatus::ServerGated);
        QCOMPARE(off.status, GateStatus::ServerGated);
        QVERIFY(missing.reason != off.reason);

        QCOMPARE(missing.reason,
                 QStringLiteral("Not enabled for this account: \"Playlists\" needs server flag "
                                "\"playlists\", which is absent from this session's flag map."));
        QCOMPARE(off.reason,
                 QStringLiteral("The server has \"Playlists\" switched off for this account (flag "
                                "\"playlists\" is present and explicitly false)."));

        // The distinction is only possible because of the presence/truth split in
        // the parser: `hasFlag` is false in both cases and only `flagsFalse`
        // separates them.
        QVERIFY(!withoutIt.capabilities().hasFlag("playlists"));
        QVERIFY(!withoutIt.capabilities().hasFlagEntry("playlists"));
        QVERIFY(!withIt.capabilities().hasFlag("playlists"));
        QVERIFY(withIt.capabilities().flagsFalse.contains("playlists"));
        QVERIFY(withIt.capabilities().hasFlagEntry("playlists"));
    }

    void everyNamedFlagIsRequiredAndTheReasonNamesTheMissingOne() {
        // AND, not OR. A surface gated on four flags is gated on all four, and the
        // reason must name the one that is absent rather than the first -- otherwise
        // a user fixes the named flag and hits the same wall again.
        const QJsonObject threeOfFour = envelope(
                R"({"flags":{"editing-stems":true,"generative-stems":true,"stem-dryer":true}})");

        GateResolver resolver;
        resolver.setCapabilities(parseSessionCapabilities(threeOfFour));
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Stems, true));

        QCOMPARE(resolver.evaluate(FeatureGate::Stems).status, GateStatus::ServerGated);
        QCOMPARE(resolver.evaluate(FeatureGate::Stems).reason,
                 QStringLiteral("Not enabled for this account: \"Stems\" needs server flag "
                                "\"crop-remove\", which is absent from this session's flag map."));
    }

    void anAvailableSentenceNamesItsFlagsAndAgreesOnNumber() {
        // The available sentence is the one place the flags are named back, so it
        // has to get the plural right: "server flag create-projects" for one,
        // "server flags a, b, c, d present" for four. A test asserting only the
        // status would pass against a sentence that listed one flag and claimed
        // there were four.
        QJsonObject env;
        env.insert(QStringLiteral("flags"), QJsonObject{{QStringLiteral("create-projects"), true},
                                                        {QStringLiteral("editing-stems"), true},
                                                        {QStringLiteral("generative-stems"), true},
                                                        {QStringLiteral("stem-dryer"), true},
                                                        {QStringLiteral("crop-remove"), true}});

        GateResolver resolver;
        resolver.setCapabilities(parseSessionCapabilities(env));
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Projects, true));
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Stems, true));

        QCOMPARE(resolver.evaluate(FeatureGate::Projects).reason,
                 QStringLiteral("Available: \"Projects\" is switched on and server flag "
                                "create-projects present."));
        QCOMPARE(resolver.evaluate(FeatureGate::Stems).reason,
                 QStringLiteral("Available: \"Stems\" is switched on and server flags "
                                "editing-stems, generative-stems, stem-dryer, crop-remove "
                                "present."));

        // And an unflagged surface says so rather than listing nothing.
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Generation, true));
        QCOMPARE(resolver.evaluate(FeatureGate::Generation).reason,
                 QStringLiteral("Available: \"Generation\" is switched on and not flag-gated."));
    }

    // ── Step 5 of six: the client switch ────────────────────────────────────

    void everySwitchStartsOffAndGenerationIsTheHeadlineCase() {
        // `Generation` is the surface this whole file was built for: the route is
        // `[T1]` and the gate is not evidence-blocked, so the only thing standing
        // between the user and a working button is a deliberate switch. Before, that
        // was a hardcoded `return false` in `SunoBridge::generationAvailable()` with
        // no way to reach `Available` at all -- a well-built door to nowhere with
        // no sentence explaining it.
        GateResolver resolver;
        const GateVerdict off = resolver.evaluate(FeatureGate::Generation);
        QCOMPARE(off.status, GateStatus::LocallyDisabled);
        QCOMPARE(off.reason,
                 QStringLiteral("Available on the server but switched off in this build: "
                                "\"Generation\". One deliberate switch away."));

        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Generation, true));
        const GateVerdict on = resolver.evaluate(FeatureGate::Generation);
        QCOMPARE(on.status, GateStatus::Available);
        QVERIFY(resolver.isAvailable(FeatureGate::Generation));

        // And off again, which is the direction that has to keep working.
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Generation, false));
        QCOMPARE(resolver.evaluate(FeatureGate::Generation).status, GateStatus::LocallyDisabled);
    }

    void aPresentServerFlagFlipsAPreviouslyUnavailableSurfaceToAvailable() {
        // The whole point of parsing the flag map: a capability flag must be able
        // to make a surface available that was not available a moment ago, without
        // a rebuild. Before this file, `flags` was read by nobody.
        GateResolver resolver;
        QCOMPARE(resolver.evaluate(FeatureGate::CustomModels).status, GateStatus::ServerGated);

        QJsonObject env;
        env.insert(QStringLiteral("flags"),
                   QJsonObject{{QStringLiteral("custom-model-ui"), true},
                               {QStringLiteral("custom-model-v6-cutover"), true}});
        resolver.setCapabilities(parseSessionCapabilities(env));
        // Flag arrived, switch still off: the verdict moved forward exactly one
        // step rather than jumping to Available. Step 5 is still there.
        QCOMPARE(resolver.evaluate(FeatureGate::CustomModels).status, GateStatus::LocallyDisabled);

        QVERIFY(resolver.setLocallyEnabled(FeatureGate::CustomModels, true));
        QCOMPARE(resolver.evaluate(FeatureGate::CustomModels).status, GateStatus::Available);

        // And the switch deliberately does not consult the flags: it is this
        // client's own intent, settable while the verdict is ServerGated, so it
        // carries information the verdict does not already carry. A production
        // surface can therefore be switched on before the account's flags land.
        GateResolver eager;
        QVERIFY(eager.setLocallyEnabled(FeatureGate::CustomModels, true));
        QCOMPARE(eager.evaluate(FeatureGate::CustomModels).status, GateStatus::ServerGated);
    }

    // ── a gate with no catalog row ──────────────────────────────────────────

    void aGateWithNoCatalogRowIsBlockedAndBlamesTheCatalog() {
        // Unreachable for every enumerator in the header -- `kFeatureGateCount` and
        // the `static_assert` guarantee one row per gate -- so this case exists to
        // pin the *defensive* half: `findFeature` returns `nullptr` rather than
        // asserting, and `evaluate` answers `EvidenceBlocked` and blames the
        // catalog rather than crashing or handing back a default `Available`.
        // A future enumerator must not turn a diagnostic into a crash.
        const auto missing = static_cast<FeatureGate>(kFeatureGateCount);
        QVERIFY(findFeature(missing) == nullptr);

        GateResolver resolver;
        const GateVerdict verdict = resolver.evaluate(missing);
        QCOMPARE(verdict.status, GateStatus::EvidenceBlocked);
        QCOMPARE(verdict.reason, QStringLiteral("No catalog entry exists for this gate."));
        QCOMPARE(verdict.gate, missing);
        QVERIFY(!resolver.isAvailable(missing));

        // The switch also refuses it, which is the fourth refusal condition and the
        // only one keyed on `findFeature` returning nothing.
        QVERIFY(!resolver.setLocallyEnabled(missing, true));
    }

    // ── catalog integrity ───────────────────────────────────────────────────

    void theCatalogHoldsExactlyOneRowPerGateInGateOrder() {
        const std::span<const FeatureDefinition> catalog = featureCatalog();
        QVERIFY(!catalog.empty());
        QCOMPARE(catalog.size(), kFeatureGateCount);

        // Index == gate. `findFeature` is a direct index rather than a search, so
        // this ordering is not a nicety: it is the reason the two cannot disagree.
        for (std::size_t i = 0; i < catalog.size(); ++i) {
            QCOMPARE(static_cast<std::size_t>(catalog[i].gate), i);
            QCOMPARE(findFeature(catalog[i].gate), &catalog[i]);
        }

        // Every enumerator resolves, so no gate can quietly resolve to nothing.
        for (std::size_t i = 0; i < kFeatureGateCount; ++i) {
            QVERIFY(findFeature(static_cast<FeatureGate>(i)) != nullptr);
        }
    }

    void theCompletenessCheckRejectsABrokenCatalog() {
        // Proves the source's `static_assert` is not vacuous. A predicate that
        // accepted everything would satisfy the real assertion just as cheerfully,
        // and the difference only shows up the day a gate loses its row.
        QVERIFY(completeAndOrdered(featureCatalog()));

        const std::span<const FeatureDefinition> catalog = featureCatalog();

        std::array<FeatureDefinition, kFeatureGateCount> shuffled{};
        std::copy(catalog.begin(), catalog.end(), shuffled.begin());
        std::swap(shuffled[0], shuffled[1]);
        QVERIFY2(!completeAndOrdered(shuffled), "an out-of-order row must be rejected");

        std::array<FeatureDefinition, kFeatureGateCount> duplicated{};
        std::copy(catalog.begin(), catalog.end(), duplicated.begin());
        duplicated[5].gate = duplicated[4].gate;
        QVERIFY2(!completeAndOrdered(duplicated), "a repeated gate must be rejected");

        std::array<FeatureDefinition, 2> tooShort{};
        std::copy(catalog.begin(), catalog.begin() + 2, tooShort.begin());
        QVERIFY2(!completeAndOrdered(tooShort), "a missing row must be rejected");
    }

    void everyCatalogRowIsADecisionAndNotADeclaration() {
        // A row without a title, an area or a note is a declaration pretending to
        // be a decision, and `note` is where the evidence grade goes to explain
        // itself. Cheap to check, and it is the check that stops the table growing
        // by accretion.
        for (const FeatureDefinition& def : featureCatalog()) {
            QVERIFY2(!def.title.empty(), gateLabel(def));
            QVERIFY2(!def.area.empty(), gateLabel(def));
            QVERIFY2(!def.note.empty(), gateLabel(def));

            // The documented area vocabulary, matching the inventory's route
            // sections. A new area is a decision to record, not a typo to discover.
            QVERIFY2(def.area == "library" || def.area == "generation" || def.area == "account" ||
                             def.area == "media" || def.area == "app" || def.area == "player" ||
                             def.area == "projects" || def.area == "discover" ||
                             def.area == "community" || def.area == "labs",
                     gateLabel(def));
        }
    }

    void catalogFlagListsAreCleanCsv() {
        // `splitFlagList` in the source tolerates `"a, b"` and `"a,b"` alike, and
        // that tolerance is right -- the catalog is hand-written. But it also means
        // a stray space in a flag name would be *silently* absorbed and turn a
        // ServerGated verdict into an unexplainable one. This asserts today's data
        // needs none of that leniency, so the tolerant branches stay unexercised.
        //
        // It cannot prove a flag *name* is right -- see the file header. What it can
        // prove is that the name is well formed, and that the row does not ask for
        // the same flag twice (which would read as two requirements and mean one).
        for (const FeatureDefinition& def : featureCatalog()) {
            const std::string_view csv = def.serverFlags;
            QVERIFY2(csv.find(' ') == std::string_view::npos, gateLabel(def));
            QVERIFY2(csv.find('\t') == std::string_view::npos, gateLabel(def));
            QVERIFY2(csv.find(',') != 0, gateLabel(def));               // leading comma
            QVERIFY2(csv.empty() || csv.back() != ',', gateLabel(def)); // trailing comma

            std::vector<std::string_view> seen;
            for (const std::string_view flag : splitStrict(csv)) {
                QVERIFY2(!flag.empty(), gateLabel(def));
                QVERIFY2(std::find(seen.begin(), seen.end(), flag) == seen.end(), gateLabel(def));
                seen.push_back(flag);
            }
        }
    }

    void everyUnblockedGateIsAvailableOnceItsOwnFlagsAndSwitchArePresent() {
        // The complement of `thePermanentlyBlockedGatesAreNamedHere`: the catalog's
        // own flag lists are *satisfiable*. Under the maximal resolver every gate
        // must land in exactly one of the three refusals or in Available, and
        // nothing may be left stranded in ServerGated or LocallyDisabled -- which
        // would mean a row names a flag the fixture could not supply, i.e. that
        // surface is unreachable no matter what the server sends.
        const GateResolver maximal = maximallyPermissiveResolver();

        int available = 0;
        int refused = 0;
        for (const FeatureDefinition& def : featureCatalog()) {
            const GateStatus status = maximal.evaluate(def.gate).status;
            QVERIFY2(status == GateStatus::Available || status == GateStatus::Excluded ||
                             status == GateStatus::EvidenceBlocked,
                     toString(def.gate));
            if (status == GateStatus::Available) {
                ++available;
            } else {
                ++refused;
            }
        }
        QCOMPARE(available, static_cast<int>(kFeatureGateCount) - 6);
        QCOMPARE(refused, 6);
    }

    void evaluateAllIsDeterministicAndInCatalogOrder() {
        // `evaluateAll()` has **no production caller** -- it was written for a
        // diagnostics panel that does not exist, which makes it the most obvious
        // first consumer and the least likely to be noticed as dead. This is
        // therefore its only guard, and the reason it is worth one: a diagnostics
        // panel that reordered its rows between renders would be its own bug, and
        // the ordering is what makes it deterministic in the first place.
        const GateResolver resolver;

        const std::vector<GateVerdict> first = resolver.evaluateAll();
        const std::vector<GateVerdict> second = resolver.evaluateAll();

        QCOMPARE(first.size(), featureCatalog().size());
        QCOMPARE(second.size(), first.size());

        for (std::size_t i = 0; i < first.size(); ++i) {
            QCOMPARE(first[i].gate, featureCatalog()[i].gate);
            QCOMPARE(first[i].status, second[i].status);
            QCOMPARE(first[i].gate, second[i].gate);
            QCOMPARE(first[i].reason, second[i].reason);
        }
    }

    void evaluateAllReportsTheSameVerdictsAsEvaluatingEachGate() {
        // `evaluateAll` is a loop, so the only way it can lie is by disagreeing
        // with `evaluate`. Pinned with a resolver that is deliberately mid-flight
        // rather than bare, because on a bare resolver every row is a refusal and a
        // wrong gate order is much harder to see.
        QJsonObject env;
        env.insert(QStringLiteral("flags"),
                   QJsonObject{{QStringLiteral("playlists"), true},
                               {QStringLiteral("playlist-condition"), true},
                               {QStringLiteral("voices-ui"), true}});

        GateResolver resolver;
        resolver.setCapabilities(parseSessionCapabilities(env));
        QVERIFY(resolver.setLocallyEnabled(FeatureGate::Playlists, true));
        // Refused, because Personas is blocked on evidence. The flag in the fixture
        // is the server saying the feature exists, which is not the same claim as
        // showing us its contract.
        QVERIFY(!resolver.setLocallyEnabled(FeatureGate::Personas, true));

        const std::vector<GateVerdict> all = resolver.evaluateAll();
        QCOMPARE(all.size(), featureCatalog().size());
        for (std::size_t i = 0; i < all.size(); ++i) {
            const GateVerdict one = resolver.evaluate(featureCatalog()[i].gate);
            QCOMPARE(all[i].status, one.status);
            QCOMPARE(all[i].reason, one.reason);
        }

        // The fixture is doing its job: the row whose flags arrived and whose switch
        // was set is Available, the evidence-blocked row beside it still refuses,
        // and an unflagged row is one step further on at LocallyDisabled. Asserted
        // by gate rather than by index -- `evaluateAll` order is catalog order, and
        // an index here would silently follow a reordering.
        QCOMPARE(resolver.evaluate(FeatureGate::Playlists).status, GateStatus::Available);
        QCOMPARE(resolver.evaluate(FeatureGate::Personas).status, GateStatus::EvidenceBlocked);
        QCOMPARE(resolver.evaluate(FeatureGate::Library).status, GateStatus::LocallyDisabled);

        int availableRows = 0;
        for (const GateVerdict& verdict : all) {
            if (verdict.status == GateStatus::Available) {
                ++availableRows;
            }
        }
        QCOMPARE(availableRows, 1);
    }
};

#include "test_FeatureFlags.moc"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TestFeatureFlags test;
    return QTest::qExec(&test, argc, argv);
}
