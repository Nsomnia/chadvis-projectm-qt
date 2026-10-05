#include <QtTest>

#include "suno/CapturedHosts.hpp"

#include <QByteArray>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

using namespace vc::suno::hosts;

namespace {

/// Every enumerator, written out. The partition tests below cannot prove a view
/// covers the registry by walking `kRegistry` alone -- what they must rule out is
/// a NEW enum value arriving with no view, and that needs a closed list to
/// compare against. Keep these in step with the headers; a missing entry here
/// fails the partition test loudly, which is the correct direction to fail.
constexpr std::array<Role, 9> kAllRoles{
    Role::StudioApi,   Role::ClerkAuth,    Role::MediaOrigin, Role::ArtworkOrigin,
    Role::CloudFront,  Role::WebApp,       Role::Realtime,    Role::UploadOrigin,
    Role::ExperimentalOrchestrator,
};

constexpr std::array<Tier, 3> kAllTiers{Tier::Production, Tier::Staging, Tier::Experimental};

/// The eleven hosts, in the order the registry is contractually sorted. Spelled
/// out rather than derived, because the assertion's whole value is that a silent
/// edit to the table -- a renamed host, a re-ordered row, a dropped entry -- fails
/// here instead of being absorbed by a helper that reads the table back at us.
constexpr std::array<std::string_view, 11> kExpectedHosts{
    "audiopipe.suno.ai",
    "auth.suno.com",
    "cdn1.suno.ai",
    "cdn2.suno.ai",
    "d2lwuy8qc234o3.cloudfront.net",
    "main.realtime.ably.net",
    "studio-api-prod.suno.com",
    "studio-api-staging.suno.com",
    "suno-ai--orpheus-prod-web.modal.run",
    "suno-uploads.s3.amazonaws.com",
    "suno.com",
};

/// `HostRule::host` as a QString so QCOMPARE prints the host on failure instead of
/// a type name. QtTest has no formatter for std::string_view, and an assertion a
/// human has to open the header to read is an assertion that gets ignored.
[[nodiscard]] QString hostOf(const HostRule& row)
{
    return QString::fromUtf8(row.host.data(), static_cast<qsizetype>(row.host.size()));
}

[[nodiscard]] QString hostOf(const std::string_view host)
{
    return QString::fromUtf8(host.data(), static_cast<qsizetype>(host.size()));
}

[[nodiscard]] QStringList hostsOf(const std::span<const HostRule> rows)
{
    QStringList hosts;
    for (const auto& row : rows) {
        hosts << hostOf(row);
    }
    return hosts;
}

/// Failure messages here are QString (they interpolate host names), while QVERIFY2
/// wants a `const char *`. One named helper does that conversion without a cast or
/// a dangling temporary at every call site: the QByteArray lives to the end of the
/// full expression, which is exactly as long as the assertion's message is needed.
[[nodiscard]] QByteArray printable(const QString& text)
{
    return text.toUtf8();
}

/// Field-for-field identity. Mirrors `containsIdentical` in the .cpp deliberately:
/// the compile-time assertion proves the two shapes agree, this proves the shape
/// they agree on is the shape a reader would call "the same row".
[[nodiscard]] bool sameRow(const HostRule& left, const HostRule& right) noexcept
{
    return left.host == right.host && left.tier == right.tier &&
           left.evidence == right.evidence && left.role == right.role &&
           left.note == right.note;
}

/// `findRule()` as a value lookup, so a test that is specifically about one known
/// row cannot dereference a null pointer when that row has been removed. Absence
/// is a legitimate outcome here -- two of these tests assert it.
[[nodiscard]] std::optional<HostRule> ruleFor(const std::string_view host) noexcept
{
    const HostRule* rule = findRule(host);
    if (rule == nullptr) {
        return std::nullopt;
    }
    return *rule;
}

[[nodiscard]] std::size_t countEvidence(const Evidence grade) noexcept
{
    std::size_t total = 0;
    for (const auto& row : hostRegistry()) {
        if (row.evidence == grade) {
            ++total;
        }
    }
    return total;
}

/// Permitted means BOTH entry points agree. `mayReceiveCredential` and
/// `classifyRefusal` are separate public functions over one implementation, and
/// the class is only worth having if the cheap predicate cannot drift away from
/// the reason the expensive one gives.
void expectPermitted(const QUrl& url, const Tier tier)
{
    QVERIFY2(!classifyRefusal(url, tier).has_value(),
             printable(QStringLiteral("expected \"%1\" to be permitted at %2, but it was refused")
                               .arg(url.toString(), toString(tier))));
    QVERIFY(mayReceiveCredential(url, tier));
}

/// Refused with one specific cause, again through both entry points. `QCOMPARE`
/// finds `toString(Refusal)` by ADL -- the reason that overload returns
/// `const char*` rather than a view -- so a failure prints
/// "Actual (evidence-too-weak) Expected (host-unknown)" and names the discrepancy
/// without anybody looking the enum up.
void expectRefused(const QUrl& url, const Tier tier, const Refusal expected)
{
    const std::optional<Refusal> refusal = classifyRefusal(url, tier);
    QVERIFY2(refusal.has_value(),
             printable(QStringLiteral("expected \"%1\" to be refused as %2 at %3, but it was "
                                     "permitted")
                               .arg(url.toString(), toString(expected), toString(tier))));
    if (!refusal.has_value()) {
        return;
    }
    QCOMPARE(*refusal, expected);
    QVERIFY(!mayReceiveCredential(url, tier));
}

} // namespace

class TestCapturedHosts : public QObject
{
    Q_OBJECT

private slots:
    // ── registry structure ──────────────────────────────────────────────────

    void theRegistryIsElevenHostsAlphabeticalAndStrictlyUnique()
    {
        // One assertion covers size, spelling, order and uniqueness at once, and
        // fails with the whole actual list beside it. The static_assert in the .cpp
        // already enforces sortedness and uniqueness at compile time -- the reason
        // this suite exists is to prove that assertion is NOT vacuous, which means
        // proving it against a list transcribed here rather than read back out of
        // the table it is meant to constrain.
        QCOMPARE(static_cast<int>(hostRegistry().size()), 11);

        QStringList expected;
        for (const std::string_view host : kExpectedHosts) {
            expected << hostOf(host);
        }
        QCOMPARE(hostsOf(hostRegistry()), expected);

        // Sortedness restated as a strict increase, so a duplicate host cannot hide
        // behind an equal-neighbour comparison and the "strictly unique" half of
        // the compile-time assertion is actually exercised.
        const auto registry = hostRegistry();
        for (std::size_t i = 1; i < registry.size(); ++i) {
            QVERIFY2(registry[i - 1].host < registry[i].host,
                     printable(QStringLiteral("%1 is not strictly below %2")
                                   .arg(hostOf(registry[i - 1]), hostOf(registry[i]))));
        }
    }

    void everyRegistryRowCarriesANonEmptyNoteAndANonTrivialGrade()
    {
        // `note` is not decoration. It is the only place an evidence grade gets to
        // explain itself, and `Evidence::Registered` is permitted by the grade gate,
        // so for such a row the note is load-bearing -- the header says so in as
        // many words. "Unknown" is the sentinel these switches return for a value
        // they do not cover, so a new enumerator that nobody handled is caught here
        // rather than rendering as a blank cell in a diagnostics panel.
        for (const auto& row : hostRegistry()) {
            QVERIFY2(!row.host.empty(), printable(QStringLiteral("a row has an empty host")));
            QVERIFY2(!row.note.empty(),
                     printable(QStringLiteral("%1 has an empty note").arg(hostOf(row))));
            QVERIFY2(std::string_view(toString(row.evidence)) != "unknown",
                     printable(QStringLiteral("%1 has an unmapped Evidence value")
                                   .arg(hostOf(row))));
            QVERIFY2(std::string_view(toString(row.role)) != "unknown",
                     printable(QStringLiteral("%1 has an unmapped Role value").arg(hostOf(row))));
            QVERIFY2(std::string_view(toString(row.tier)) != "unknown",
                     printable(QStringLiteral("%1 has an unmapped Tier value").arg(hostOf(row))));
        }
    }

    void theTierCensusIsNineProductionOneStagingOneExperimental()
    {
        // Hardcoded on purpose, and the numbers are NOT derivable from each other:
        // CapturedHosts.hpp:200 documents this view as "one at Staging, one at
        // Experimental, eight at Production" while the table has nine at
        // Production. That header comment is stale and the table is right (this
        // test is the measurement), so a census written as "expected = whatever the
        // views sum to" would happily agree with either. These numbers are what a
        // future session should check before believing a "production only" claim.
        QCOMPARE(static_cast<int>(hostsForTier(Tier::Production).size()), 9);
        QCOMPARE(static_cast<int>(hostsForTier(Tier::Staging).size()), 1);
        QCOMPARE(static_cast<int>(hostsForTier(Tier::Experimental).size()), 1);

        QCOMPARE(hostsOf(hostsForTier(Tier::Staging)),
                 QStringList({QStringLiteral("studio-api-staging.suno.com")}));
        QCOMPARE(hostsOf(hostsForTier(Tier::Experimental)),
                 QStringList({QStringLiteral("suno-ai--orpheus-prod-web.modal.run")}));

        // The nine, so a host silently moved from Staging to Production fails here.
        QCOMPARE(hostsOf(hostsForTier(Tier::Production)),
                 QStringList({QStringLiteral("audiopipe.suno.ai"),
                              QStringLiteral("auth.suno.com"),
                              QStringLiteral("cdn1.suno.ai"),
                              QStringLiteral("cdn2.suno.ai"),
                              QStringLiteral("d2lwuy8qc234o3.cloudfront.net"),
                              QStringLiteral("main.realtime.ably.net"),
                              QStringLiteral("studio-api-prod.suno.com"),
                              QStringLiteral("suno-uploads.s3.amazonaws.com"),
                              QStringLiteral("suno.com")}));
    }

    void theRoleCensusIsTwoStudioApiTwoArtworkAndOneOfEveryOtherRole()
    {
        // Two roles carry two hosts and the other seven carry one each, which is
        // 11. Both doubles are deliberate: StudioApi spans both tiers of the one
        // API surface, and the cdn1/cdn2 artwork pair is two captured hosts rather
        // than a wildcard, because "some Suno CDN" is not a captured fact.
        QCOMPARE(static_cast<int>(hostsForRole(Role::StudioApi).size()), 2);
        QCOMPARE(static_cast<int>(hostsForRole(Role::ArtworkOrigin).size()), 2);

        std::size_t total = 0;
        for (const Role role : kAllRoles) {
            if (role != Role::StudioApi && role != Role::ArtworkOrigin) {
                QCOMPARE(static_cast<int>(hostsForRole(role).size()), 1);
            }
            total += hostsForRole(role).size();
        }
        QCOMPARE(static_cast<int>(total), static_cast<int>(hostRegistry().size()));
    }

    void theGradeCensusIsNineCapturedOneResponseValueAndOneLead()
    {
        // Zero rows graded `Registered`, asserted deliberately. The grade gate
        // permits `Registered` and the next test pins that, so the two facts have
        // to be pinned together: the day someone adds a `Registered` row, the gate
        // is no longer theoretical and this number is what says so. Same for the
        // one `ResponseValue` row, which is the realtime push host and the whole
        // subject of the most important assertion in this file.
        QCOMPARE(static_cast<int>(countEvidence(Evidence::Captured)), 9);
        QCOMPARE(static_cast<int>(countEvidence(Evidence::ResponseValue)), 1);
        QCOMPARE(static_cast<int>(countEvidence(Evidence::Registered)), 0);
        QCOMPARE(static_cast<int>(countEvidence(Evidence::Lead)), 1);

        const auto responseValue = ruleFor("main.realtime.ably.net");
        QVERIFY(responseValue.has_value());
        QCOMPARE(responseValue->evidence, Evidence::ResponseValue);

        const auto lead = ruleFor("suno-ai--orpheus-prod-web.modal.run");
        QVERIFY(lead.has_value());
        QCOMPARE(lead->evidence, Evidence::Lead);
    }

    void everyRoleViewPartitionContainsOnlyIdenticalRegistryRows()
    {
        // Half of `registryIsConsistent()`. A row invented inside a per-role view
        // -- a host literal pasted into kMediaOrigins without ever reaching
        // kRegistry -- would be reachable through hostsForRole() and invisible to
        // every credential decision that iterates the registry.
        for (const Role role : kAllRoles) {
            for (const auto& row : hostsForRole(role)) {
                const HostRule* master = findRule(row.host);
                QVERIFY2(master != nullptr,
                         printable(QStringLiteral("%1 is in hostsForRole(%2) but not in the "
                                                 "registry")
                                         .arg(hostOf(row), toString(role))));
                QVERIFY2(sameRow(row, *master),
                         printable(QStringLiteral("%1 differs between its role view and the "
                                                 "registry")
                                         .arg(hostOf(row))));
            }
        }
    }

    void everyTierViewPartitionContainsOnlyIdenticalRegistryRows()
    {
        for (const Tier tier : kAllTiers) {
            for (const auto& row : hostsForTier(tier)) {
                const HostRule* master = findRule(row.host);
                QVERIFY2(master != nullptr,
                         printable(QStringLiteral("%1 is in hostsForTier(%2) but not in the "
                                                 "registry")
                                         .arg(hostOf(row), toString(tier))));
                QVERIFY2(sameRow(row, *master),
                         printable(QStringLiteral("%1 differs between its tier view and the "
                                                 "registry")
                                         .arg(hostOf(row))));
                // And the row's own tier field agrees with the view it sits in --
                // otherwise hostsForTier(Production) could return a Staging host and
                // the partition arithmetic would still add up.
                QCOMPARE(row.tier, tier);
            }
        }
    }

    void noHostAppearsInTwoRoleViewsOrInTwoTierViews()
    {
        // "Partition" means disjoint, and `registryIsConsistent()` only checks the
        // sums add up -- a host listed twice in two different role arrays passes
        // every compile-time assertion in the file while making one role's view
        // silently cover a host the caller thinks belongs to somebody else.
        for (const Role first : kAllRoles) {
            for (const auto& row : hostsForRole(first)) {
                for (const Role second : kAllRoles) {
                    if (first == second) {
                        continue;
                    }
                    for (const auto& other : hostsForRole(second)) {
                        QVERIFY2(other.host != row.host,
                                 printable(QStringLiteral("%1 appears in both Role::%2 and "
                                                         "Role::%3")
                                                 .arg(hostOf(row), toString(first),
                                                      toString(second))));
                    }
                }
            }
        }
        for (const Tier first : kAllTiers) {
            for (const Tier second : kAllTiers) {
                if (first != second) {
                    for (const auto& row : hostsForTier(first)) {
                        for (const auto& other : hostsForTier(second)) {
                            QVERIFY2(other.host != row.host,
                                     printable(QStringLiteral("%1 appears at both Tier::%2 and "
                                                             "Tier::%3")
                                                     .arg(hostOf(row), toString(first),
                                                          toString(second))));
                        }
                    }
                }
            }
        }
    }

    void everyRegistryRowIsReachableThroughBothOfItsViews()
    {
        // The other half of `registryIsConsistent()`, and the direction that
        // actually catches the bug the compile-time assertion exists for: a host
        // ADDED to kRegistry and forgotten in a view. It would compile, it would
        // pass every sortedness and uniqueness check, and it would be unreachable
        // through both hostsForRole() and hostsForTier() with no diagnostic
        // anywhere -- while still passing the size-sum arithmetic only until the
        // next host arrives to unbalance it.
        for (const auto& row : hostRegistry()) {
            const auto byRole = hostsForRole(row.role);
            const auto byTier = hostsForTier(row.tier);

            int roleMatches = 0;
            for (const auto& candidate : byRole) {
                if (candidate.host == row.host) {
                    ++roleMatches;
                    QVERIFY(sameRow(candidate, row));
                }
            }
            int tierMatches = 0;
            for (const auto& candidate : byTier) {
                if (candidate.host == row.host) {
                    ++tierMatches;
                    QVERIFY(sameRow(candidate, row));
                }
            }
            QVERIFY2(roleMatches == 1,
                     printable(QStringLiteral("%1 appears %2 time(s) in hostsForRole(%3)")
                                     .arg(hostOf(row))
                                     .arg(roleMatches)
                                     .arg(toString(row.role))));
            QVERIFY2(tierMatches == 1,
                     printable(QStringLiteral("%1 appears %2 time(s) in hostsForTier(%3)")
                                     .arg(hostOf(row))
                                     .arg(tierMatches)
                                     .arg(toString(row.tier))));
        }
    }

    void everyRoleResolvesToTheHostSetItClaims()
    {
        // The per-role views are what the tree is meant to adopt in place of the
        // hardcoded literals this file replaced, so each one is pinned by name.
        // Notably suno-uploads.s3.amazonaws.com: it is here because
        // SunoAudioUploadService already contacts it with a literal, and a registry
        // that omitted a host the tree reaches would not be the single owner it
        // claims to be -- the next person to adopt this table for uploads would find
        // no row and conclude the host was unverified.
        QCOMPARE(hostsOf(hostsForRole(Role::StudioApi)),
                 QStringList({QStringLiteral("studio-api-prod.suno.com"),
                              QStringLiteral("studio-api-staging.suno.com")}));
        QCOMPARE(hostsOf(hostsForRole(Role::ClerkAuth)),
                 QStringList({QStringLiteral("auth.suno.com")}));
        QCOMPARE(hostsOf(hostsForRole(Role::MediaOrigin)),
                 QStringList({QStringLiteral("audiopipe.suno.ai")}));
        QCOMPARE(hostsOf(hostsForRole(Role::ArtworkOrigin)),
                 QStringList({QStringLiteral("cdn1.suno.ai"), QStringLiteral("cdn2.suno.ai")}));
        QCOMPARE(hostsOf(hostsForRole(Role::CloudFront)),
                 QStringList({QStringLiteral("d2lwuy8qc234o3.cloudfront.net")}));
        QCOMPARE(hostsOf(hostsForRole(Role::WebApp)), QStringList({QStringLiteral("suno.com")}));
        QCOMPARE(hostsOf(hostsForRole(Role::Realtime)),
                 QStringList({QStringLiteral("main.realtime.ably.net")}));
        QCOMPARE(hostsOf(hostsForRole(Role::UploadOrigin)),
                 QStringList({QStringLiteral("suno-uploads.s3.amazonaws.com")}));
        QCOMPARE(hostsOf(hostsForRole(Role::ExperimentalOrchestrator)),
                 QStringList({QStringLiteral("suno-ai--orpheus-prod-web.modal.run")}));
    }

    void anUnmappedRoleOrTierResolvesToNoHostsAtAll()
    {
        // The `return {};` fallbacks at the end of both switches are the fail-closed
        // branch: an enumerator this build does not know must yield an empty view
        // rather than the last case's rows. Both enums have a fixed underlying
        // type, so the cast is well defined and 999 is out of range of any real
        // enumerator rather than accidentally aliasing one.
        QCOMPARE(hostsForRole(static_cast<Role>(999)).size(), std::size_t{0});
        QCOMPARE(hostsForTier(static_cast<Tier>(999)).size(), std::size_t{0});
    }

    // ── the evidence-grade gate ─────────────────────────────────────────────

    void onlyCapturedAndRegisteredGradesPermitACredential()
    {
        // `Registered` passing is the surprising half and is pinned on purpose.
        // It is correct rather than lax: the way a route's existence gets
        // established is by sending it a request and being answered 401, so a
        // `Registered` host has demonstrably already received a credential and
        // refused it. Nobody has a `Registered` row today (the census above), so
        // this is the policy's statement rather than a live grant.
        QVERIFY(evidencePermitsCredential(Evidence::Captured));
        QVERIFY(evidencePermitsCredential(Evidence::Registered));

        // The two refusals. ResponseValue is the grade that exists BECAUSE
        // conflating it with Registered is a credential-disclosure hole rather
        // than a nicety, so its refusal is the newer and stricter of the two.
        QVERIFY(!evidencePermitsCredential(Evidence::ResponseValue));
        QVERIFY(!evidencePermitsCredential(Evidence::Lead));
    }

    void theRealtimePushHostIsGradedResponseValueAndCanNeverReceiveTheBearer()
    {
        // ── THE most important assertion in this file ──────────────────────────
        // `main.realtime.ably.net` is the one host the table ever graded
        // `Registered`, and that grade PERMITS a credential -- so under it the only
        // thing standing between the Suno bearer and Ably's third-party realtime
        // infrastructure was a prose note in a table row. Notes get truncated by
        // editors and refactored away; this is compiled, and a `static_assert` on
        // the grade gate backs it up.
        //
        // The host's only appearance in the whole evidence corpus is a `stream_url`
        // VALUE inside a `GET /api/realtime/discover` response body. Zero requests
        // were ever sent there by any capture, so there is no evidence any
        // credential belongs there (AGENTS.md 1). If this test ever fails, the fix
        // is a captured request to that host -- not a grade change, not a refactor,
        // and not a note.
        const auto rule = ruleFor("main.realtime.ably.net");
        QVERIFY(rule.has_value());
        QCOMPARE(rule->evidence, Evidence::ResponseValue);
        QCOMPARE(rule->tier, Tier::Production);
        QCOMPARE(rule->role, Role::Realtime);

        const QUrl url{QStringLiteral("https://main.realtime.ably.net/sse?v=1.2&enveloped=false")};
        expectRefused(url, Tier::Production, Refusal::EvidenceTooWeak);

        // At no tier. At Staging the reported cause is the tier mismatch, because
        // the tier check runs before the grade check; that ordering is not a
        // security hole (both refuse) but it is asserted separately so a swap of
        // the two cannot hide a grade regression from a test that only looks at the
        // Production tier.
        for (const Tier tier : kAllTiers) {
            QVERIFY2(!mayReceiveCredential(url, tier),
                     printable(QStringLiteral("the realtime push origin accepted a credential "
                                             "at %1")
                                   .arg(toString(tier))));
        }
    }

    void theBundleStringOrchestratorHostIsRefusedAtEveryTier()
    {
        // suno-ai--orpheus-prod-web.modal.run is a `[LEAD]`: a string in a shipped
        // bundle, never requested by any capture. It gets its OWN refusal cause
        // rather than a confusing tier mismatch, because a user reading "tier not
        // permitted" would reasonably enable staging and then conclude the client
        // was broken -- the sentence is the whole point of the separate enumerator.
        //
        // Note what the cause is NOT, and this is deliberate rather than a missing
        // branch: it is not `EvidenceTooWeak`, even though the row is graded `Lead`
        // and `Lead` fails the grade gate. The Experimental check runs first, so
        // today `EvidenceTooWeak` is reachable only for a `ResponseValue` host.
        // A future `Lead`-graded host at a non-Experimental tier is the case that
        // would make the grade gate's `Lead` arm live at runtime.
        for (const Tier tier : kAllTiers) {
            expectRefused(
                    QUrl{QStringLiteral("https://suno-ai--orpheus-prod-web.modal.run/session-history")},
                    tier, Refusal::ExperimentalRefused);
        }
    }

    void aTierMismatchOutranksTheEvidenceGradeInTheReportedCause()
    {
        // The realtime host is `ResponseValue` at the Production tier, so asking
        // about it under Staging exercises which of the two checks comes first. It
        // is the tier. Diagnosis quality rather than security -- both orders refuse
        // -- but a test that only asserted "refused" would not notice the two
        // checks being swapped, and then the sentence a user reads would name a
        // cause that is not the one that fired.
        expectRefused(QUrl{QStringLiteral("https://main.realtime.ably.net/sse")}, Tier::Staging,
                      Refusal::TierNotPermitted);
    }

    void theExperimentalTierHasOneHostAndIsNotSelectable()
    {
        // The two halves of the same invariant, pinned together: the registry does
        // list an Experimental host, and TierPolicy must never be raised to it.
        // `isSelectableTier` refusing it is why `raiseTo` returns false there
        // instead of reporting success and leaving the client able to talk to
        // nothing -- matching is exact, so an Experimental-only active tier could
        // only ever narrow the reachable set to a host that refuses everything.
        QCOMPARE(static_cast<int>(hostsForTier(Tier::Experimental).size()), 1);
        QVERIFY(!isSelectableTier(Tier::Experimental));
        QVERIFY(isSelectableTier(Tier::Production));
        QVERIFY(isSelectableTier(Tier::Staging));
    }

    // ── the credential gate: permitted ──────────────────────────────────────

    void aCapturedStudioApiUrlMayReceiveACredential()
    {
        // The positive case, stated first and exactly. Without it every other test
        // in this file would be satisfied by a predicate that refuses everything.
        expectPermitted(QUrl{QStringLiteral("https://studio-api-prod.suno.com/api/session/")},
                        Tier::Production);
    }

    void anExplicitDefaultPortIsStillTheDefaultPort()
    {
        // Measured on Qt 6.11.1: QUrl::port() returns 443 for an explicitly written
        // :443 and -1 when no port is given, and the policy accepts both. Asserted
        // because the two are different code paths in a captured URL (a rewritten
        // or echoed URL can carry the port explicitly) and a future "simplification"
        // to `port() == 443` would silently refuse the unadorned form.
        expectPermitted(QUrl{QStringLiteral("https://studio-api-prod.suno.com/api/session/")},
                        Tier::Production);
        expectPermitted(QUrl{QStringLiteral("https://studio-api-prod.suno.com:443/api/session/")},
                        Tier::Production);
    }

    void schemeAndHostCaseDoNotDefeatTheGate()
    {
        // Measured on Qt 6.11.1: QUrl normalises BOTH the scheme and the host to
        // lower case during parsing, so this pins that normalisation happening and
        // the policy not fighting it -- the case-insensitive scheme comparison in
        // classifyRefusal is belt-and-braces against a future QUrl. The ASCII fold
        // itself is exercised for real in the findRule test below, which takes a
        // string_view and therefore gets no QUrl normalisation at all.
        expectPermitted(
                QUrl{QStringLiteral("HTTPS://Studio-API-Prod.Suno.COM/api/session/")},
                Tier::Production);
    }

    // ── the credential gate: refused, one cause per case ────────────────────

    void aRelativeUrlIsRefusedAsInvalid()
    {
        // First check in classifyRefusal, and it has to be first: an allowlist
        // cannot be evaluated against a target that is not an absolute URL. A
        // default-constructed QUrl is the degenerate version of the same case.
        expectRefused(QUrl(), Tier::Production, Refusal::InvalidUrl);
        expectRefused(QUrl{QStringLiteral("/api/session/")}, Tier::Production,
                      Refusal::InvalidUrl);
    }

    void aMalformedUrlIsRefusedAsInvalid()
    {
        // Measured: QUrl reports a space inside the host as invalid, so this is the
        // isValid() half of the same first check rather than the isRelative() half.
        // Worth its own case because "malformed" and "relative" fail for different
        // reasons and a future refactor that drops isValid() would still pass a
        // relative-URL test.
        const QUrl malformed{QStringLiteral("https://exa mple.com/api/session/")};
        QVERIFY(!malformed.isValid());
        expectRefused(malformed, Tier::Production, Refusal::InvalidUrl);
    }

    void aProtocolRelativeUrlIsRefusedEvenThoughItsHostIsCaptured()
    {
        // Measured: QUrl calls "//host/path" relative, because there is no scheme.
        // The host here IS in the registry, so this is the case where the first
        // check is the only thing standing between a protocol-relative URL and a
        // permitted credential -- and it is permitted-looking enough to be copied
        // into a caller by someone who checked the host.
        expectRefused(QUrl{QStringLiteral("//studio-api-prod.suno.com/api/session/")},
                      Tier::Production, Refusal::InvalidUrl);
    }

    void aUrlWithNoHostIsRefusedAsHostUnknown()
    {
        // Measured: "https://" parses as a valid, non-relative URL with an empty
        // host. So it falls all the way through to the registry lookup and is
        // reported as an unknown HOST rather than an invalid URL -- correct, and
        // pinned because "invalid" is the answer a reader expects and the
        // distinction is what keeps a malformed-URL metric honest.
        const QUrl empty{QStringLiteral("https://")};
        QVERIFY(empty.isValid());
        QVERIFY(!empty.isRelative());
        QVERIFY(empty.host().isEmpty());
        expectRefused(empty, Tier::Production, Refusal::HostUnknown);
    }

    void plainHttpIsRefusedBeforeItsPortIsConsidered()
    {
        // Scheme is checked before the port, so this is SchemeNotHttps even with an
        // explicit 443. A credential sent over anything but https is sent in the
        // clear, which is a worse outcome than talking to the wrong port.
        expectRefused(QUrl{QStringLiteral("http://studio-api-prod.suno.com/api/session/")},
                      Tier::Production, Refusal::SchemeNotHttps);
        expectRefused(QUrl{QStringLiteral("http://studio-api-prod.suno.com:443/api/session/")},
                      Tier::Production, Refusal::SchemeNotHttps);
    }

    void aNonDefaultPortIsRefusedEvenOnACapturedHost()
    {
        // A non-default port is a different origin, and no capture ever saw a
        // credential sent to one. The host here is the primary studio API, so this
        // is the case where the port rule has to fire on its own.
        expectRefused(QUrl{QStringLiteral("https://studio-api-prod.suno.com:8443/api/session/")},
                      Tier::Production, Refusal::NonDefaultPort);
    }

    void aPortAboveTheSignedSixteenBitRangeIsStillRefused()
    {
        // Regression guard for a specific wrong belief recorded in
        // CapturedHosts.cpp:417: "QUrl::port() returns qint16". It does not -- Qt
        // 6.11 declares `int port(int defaultPort = -1) const`. Under the qint16
        // belief a port of 40000 comes back as a negative number, the `port() < 0`
        // branch treats it as the default port, and the check silently ALLOWS a
        // credential to a captured host on an attacker-chosen port. Measured here:
        // 40000 and 65535 both come back as themselves and are refused.
        const QUrl high{QStringLiteral("https://studio-api-prod.suno.com:40000/api/session/")};
        QCOMPARE(high.port(), 40000);
        expectRefused(high, Tier::Production, Refusal::NonDefaultPort);

        const QUrl highest{QStringLiteral("https://studio-api-prod.suno.com:65535/api/session/")};
        QCOMPARE(highest.port(), 65535);
        expectRefused(highest, Tier::Production, Refusal::NonDefaultPort);
    }

    void userinfoIsRefused()
    {
        // Captured Suno URLs never carry userinfo, and userinfo in an absolute URL
        // is the classic way to make a host look like something it is not -- both
        // the bare "user@" form and the "user:password@" form.
        expectRefused(QUrl{QStringLiteral("https://user@studio-api-prod.suno.com/api/")},
                      Tier::Production, Refusal::UserInfoPresent);
        expectRefused(
                QUrl{QStringLiteral("https://user:password@studio-api-prod.suno.com/api/")},
                Tier::Production, Refusal::UserInfoPresent);
    }

    void aHostOutsideTheRegistryIsRefused()
    {
        // AGENTS.md 1: fail closed on any host absent from a captured allowlist.
        // The host used here is a real Suno service hostname, not a nonsense one,
        // so the test fails for the reason the rule exists and not for an obvious
        // typo -- a nonsense hostname would also be refused, just for a different
        // reason, and would prove nothing about the allowlist.
        expectRefused(QUrl{QStringLiteral("https://api.suno.ai/api/feed/v3/")}, Tier::Production,
                      Refusal::HostUnknown);
        expectRefused(QUrl{QStringLiteral("https://evil.example.com/")}, Tier::Production,
                      Refusal::HostUnknown);
    }

    void theFaviconAndManifestHostIsNotInTheRegistry()
    {
        // cdn-o.suno.com is the concrete shape of a host that LOOKS captured: the
        // 2026-09-28 recon finds it 8 times in the corpus and every one of those is
        // a string literal, with zero direct requests to it. It serves favicons and
        // the PWA manifest, so it is exactly the sort of host someone adds "because
        // the bundle mentions it". The master excludes it by name; pinned here so
        // the exclusion cannot be undone by a well-meaning image request.
        expectRefused(QUrl{QStringLiteral("https://cdn-o.suno.com/favicon.ico")}, Tier::Production,
                      Refusal::HostUnknown);
        QVERIFY(!ruleFor("cdn-o.suno.com").has_value());
    }

    void aTrailingDotHostIsRefusedAsHostUnknown()
    {
        // Deliberate, and pinned so a future "fix" is a conscious act rather than a
        // drive-by. "studio-api-prod.suno.com." is the same origin as
        // "studio-api-prod.suno.com" in DNS terms, so accepting it would be
        // correct-but-widening; rejecting it falls through to HostUnknown, the
        // fail-closed answer this file exists to produce. Measured: QUrl keeps the
        // trailing dot in host(), so it really does reach the lookup unnormalised.
        const QUrl trailing{QStringLiteral("https://studio-api-prod.suno.com./api/session/")};
        QCOMPARE(trailing.host(), QStringLiteral("studio-api-prod.suno.com."));
        expectRefused(trailing, Tier::Production, Refusal::HostUnknown);
        QVERIFY(!ruleFor("studio-api-prod.suno.com.").has_value());
    }

    void aStagingHostIsRefusedWhileProductionIsActive()
    {
        // The default posture: a captured Staging host is real and reachable, and is
        // still refused until somebody asks for it.
        expectRefused(QUrl{QStringLiteral("https://studio-api-staging.suno.com/api/session/")},
                      Tier::Production, Refusal::TierNotPermitted);
    }

    // ── tier semantics ──────────────────────────────────────────────────────

    void aDefaultPolicyIsProductionWithStagingOff()
    {
        // Fail closed by construction: there is no constructor that takes a tier, so
        // no call site can enable staging by forgetting that it should not have.
        const TierPolicy policy;
        QCOMPARE(policy.activeTier(), Tier::Production);
        QVERIFY(!policy.stagingEnabled());
    }

    void raisingToStagingNarrowsTheReachableSetRatherThanWideningIt()
    {
        // Tier matching in mayReceiveCredential is EXACT, not a floor. Raising the
        // tier therefore NARROWS the reachable set to that tier rather than adding
        // to production -- so "I enabled staging" can never be a sentence that also
        // means "and I kept talking to prod".
        //
        // This is the decision a future reader is most likely to read as a bug, so
        // it is asserted in both directions in one case: a regression that made
        // raising the tier additive would fail the second half just as loudly as
        // one that made it refuse staging entirely fails the first.
        const QUrl productionUrl{
            QStringLiteral("https://studio-api-prod.suno.com/api/session/")};
        const QUrl stagingUrl{QStringLiteral("https://studio-api-staging.suno.com/api/session/")};

        TierPolicy policy;
        expectPermitted(productionUrl, policy.activeTier());
        expectRefused(stagingUrl, policy.activeTier(), Refusal::TierNotPermitted);

        QVERIFY(policy.raiseTo(Tier::Staging));
        QCOMPARE(policy.activeTier(), Tier::Staging);
        QVERIFY(policy.stagingEnabled());

        expectPermitted(stagingUrl, policy.activeTier());
        expectRefused(productionUrl, policy.activeTier(), Refusal::TierNotPermitted);
    }

    void raisingToExperimentalIsRefusedAndChangesNothing()
    {
        // The registry lists an Experimental host and that host refuses everything,
        // so an Experimental active tier could only ever narrow the reachable set
        // to nothing. Returning false -- rather than reporting success -- is the
        // honest answer, and the unchanged active tier is asserted because a
        // half-applied mutation is the failure mode that would actually hurt.
        TierPolicy policy;
        QVERIFY(!policy.raiseTo(Tier::Experimental));
        QCOMPARE(policy.activeTier(), Tier::Production);
        QVERIFY(!policy.stagingEnabled());
    }

    void raisingToTheActiveTierIsIdempotent()
    {
        TierPolicy policy;
        QVERIFY(policy.raiseTo(Tier::Production));
        QCOMPARE(policy.activeTier(), Tier::Production);
        QVERIFY(!policy.stagingEnabled());
    }

    void aStagingPolicyCanBeRaisedBackToProduction()
    {
        // Staging is not a one-way door, so `raiseTo` -- not just
        // resetToProduction() -- has to be able to narrow back. This is also the
        // only path that does not need a separate mutator, which is the point of
        // keeping raiseTo as the single mutator.
        TierPolicy policy;
        QVERIFY(policy.raiseTo(Tier::Staging));
        QVERIFY(policy.raiseTo(Tier::Production));
        QCOMPARE(policy.activeTier(), Tier::Production);
        expectPermitted(QUrl{QStringLiteral("https://studio-api-prod.suno.com/api/session/")},
                        policy.activeTier());
    }

    void resetToProductionRestoresTheDefault()
    {
        TierPolicy policy;
        QVERIFY(policy.raiseTo(Tier::Staging));
        policy.resetToProduction();
        QCOMPARE(policy.activeTier(), Tier::Production);
        QVERIFY(!policy.stagingEnabled());
        expectRefused(QUrl{QStringLiteral("https://studio-api-staging.suno.com/api/session/")},
                      policy.activeTier(), Refusal::TierNotPermitted);
    }

    // ── diagnostics ─────────────────────────────────────────────────────────

    void describeRefusalIsEmptyForAPermittedUrl()
    {
        // Deliberately empty rather than "permitted": a caller can show nothing
        // without a special case. The sharp edge, and the reason the header calls it
        // out, is that the SAME function then answers "no reason" and "allowed"
        // identically -- so a diagnostics surface cannot tell them apart from the
        // return value alone and has to ask mayReceiveCredential() as well.
        const TierPolicy policy;
        QVERIFY(policy.describeRefusal(
                        QUrl{QStringLiteral("https://studio-api-prod.suno.com/api/session/")},
                        policy.activeTier())
                        .isEmpty());
    }

    void describeRefusalNamesTheCause_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<int>("tier");
        QTest::addColumn<QString>("cause");

        QTest::newRow("invalid-url") << QStringLiteral("/api/session/")
                                     << static_cast<int>(Tier::Production)
                                     << QStringLiteral("not a valid absolute URL");
        QTest::newRow("scheme-not-https")
            << QStringLiteral("http://studio-api-prod.suno.com/api/")
            << static_cast<int>(Tier::Production) << QStringLiteral("not https");
        QTest::newRow("non-default-port")
            << QStringLiteral("https://studio-api-prod.suno.com:8443/api/")
            << static_cast<int>(Tier::Production)
            << QStringLiteral("neither the default port nor 443");
        QTest::newRow("userinfo-present")
            << QStringLiteral("https://user@studio-api-prod.suno.com/api/")
            << static_cast<int>(Tier::Production) << QStringLiteral("carries userinfo");
        QTest::newRow("host-unknown") << QStringLiteral("https://evil.example.com/")
                                       << static_cast<int>(Tier::Production)
                                       << QStringLiteral("not in the captured-host registry");
        QTest::newRow("tier-not-permitted")
            << QStringLiteral("https://studio-api-staging.suno.com/api/")
            << static_cast<int>(Tier::Production) << QStringLiteral("Tier matching is exact");
        QTest::newRow("experimental-refused")
            << QStringLiteral("https://suno-ai--orpheus-prod-web.modal.run/x")
            << static_cast<int>(Tier::Production)
            << QStringLiteral("never permitted to receive a credential");
        QTest::newRow("evidence-too-weak")
            << QStringLiteral("https://main.realtime.ably.net/sse")
            << static_cast<int>(Tier::Production) << QStringLiteral("response-value");
    }

    void describeRefusalNamesTheCause()
    {
        QFETCH(QString, url);
        QFETCH(int, tier);
        QFETCH(QString, cause);

        const TierPolicy policy;
        const QString sentence = policy.describeRefusal(QUrl{url}, static_cast<Tier>(tier));

        QVERIFY2(!sentence.isEmpty(),
                 printable(QStringLiteral("no sentence for \"%1\"").arg(url)));
        QVERIFY2(sentence.startsWith(QStringLiteral("Refused ")),
                 printable(QStringLiteral("%1 does not announce itself as a refusal")
                               .arg(sentence)));
        // The URL is echoed back, because a refusal a user cannot correlate with
        // the request they made is not a diagnosis.
        QVERIFY2(sentence.contains(url), printable(sentence));
        QVERIFY2(sentence.contains(cause), printable(sentence));
    }

    void everyReachableRefusalProducesItsOwnSentence()
    {
        // Distinctness is the whole point of a named cause: a diagnostics panel has
        // to be able to tell the user WHY. Note that the sentences below are not
        // trivially distinct -- each embeds its own URL, which is what makes the
        // per-cause substring in the data-driven test above the assertion that
        // actually carries this one.
        const std::array<QPair<QString, Tier>, 8> cases{{
            {QStringLiteral("/api/session/"), Tier::Production},
            {QStringLiteral("http://studio-api-prod.suno.com/api/"), Tier::Production},
            {QStringLiteral("https://studio-api-prod.suno.com:8443/api/"), Tier::Production},
            {QStringLiteral("https://user@studio-api-prod.suno.com/api/"), Tier::Production},
            {QStringLiteral("https://evil.example.com/"), Tier::Production},
            {QStringLiteral("https://studio-api-staging.suno.com/api/"), Tier::Production},
            {QStringLiteral("https://suno-ai--orpheus-prod-web.modal.run/x"), Tier::Production},
            {QStringLiteral("https://main.realtime.ably.net/sse"), Tier::Production},
        }};

        const TierPolicy policy;
        QStringList sentences;
        for (const auto& [url, tier] : cases) {
            const QString sentence = policy.describeRefusal(QUrl{url}, tier);
            QVERIFY2(!sentence.isEmpty(), printable(url));
            sentences << sentence;
        }
        QCOMPARE(sentences.size(), cases.size());
        QCOMPARE(QSet<QString>(sentences.cbegin(), sentences.cend()).size(), cases.size());
    }

    void describeRefusalAnswersACounterfactualWithoutMutatingThePolicy()
    {
        // The tier is a parameter rather than read from activeTier_ on purpose: a
        // diagnostics panel can ask "what would this say if staging were on?"
        // without mutating policy to find out. Asserted against the live policy,
        // which is the only way to prove the counterfactual answer is not quietly
        // going through raiseTo() on the way.
        const TierPolicy policy;
        const QUrl productionUrl{
            QStringLiteral("https://studio-api-prod.suno.com/api/session/")};
        const QUrl stagingUrl{QStringLiteral("https://studio-api-staging.suno.com/api/session/")};

        QCOMPARE(policy.activeTier(), Tier::Production);
        QVERIFY(policy.describeRefusal(productionUrl, Tier::Production).isEmpty());
        QVERIFY(!policy.describeRefusal(productionUrl, Tier::Staging).isEmpty());
        QVERIFY(!policy.describeRefusal(stagingUrl, Tier::Production).isEmpty());
        QVERIFY(policy.describeRefusal(stagingUrl, Tier::Staging).isEmpty());
        QCOMPARE(policy.activeTier(), Tier::Production);
    }

    void describeRefusalAgreesWithThePredicateForEveryHostAtEveryTier()
    {
        // Every registered host against every tier: 33 combinations, and exactly
        // NINE of them permitted. The count is worth spelling out, because the
        // obvious arithmetic gets it wrong:
        //
        //   9 Production hosts at the Production tier  -> but the realtime push
        //       origin is ResponseValue, so it refuses, leaving EIGHT
        //   1 Staging host at the Staging tier          -> one
        //   1 Experimental host at the Experimental tier -> refused, permanently
        //
        // So 8 + 1 = 9, and the grade gate costs exactly one of the ten pairs the
        // tier structure alone would allow. That is the whole point of keeping the
        // two axes orthogonal: this number moves if either is tampered with.
        const TierPolicy policy;
        int permittedPairs = 0;
        for (const auto& row : hostRegistry()) {
            for (const Tier tier : kAllTiers) {
                const QUrl url{QStringLiteral("https://%1/api/session/").arg(hostOf(row))};
                const bool permitted = mayReceiveCredential(url, tier);
                QCOMPARE(policy.describeRefusal(url, tier).isEmpty(), permitted);
                if (permitted) {
                    ++permittedPairs;
                }
            }
        }
        QCOMPARE(permittedPairs, 9);
    }

    void everyTierAndRefusalNameIsDistinctAndStable()
    {
        // The names are what a log line, an assertion failure and a diagnostics
        // panel all quote, so they are pinned verbatim rather than merely for
        // uniqueness -- "unknown" appearing in a user-facing string because a new
        // enumerator was added without a case is a bug this catches.
        QCOMPARE(toString(Tier::Production), "production");
        QCOMPARE(toString(Tier::Staging), "staging");
        QCOMPARE(toString(Tier::Experimental), "experimental");

        QCOMPARE(toString(Evidence::Captured), "captured");
        QCOMPARE(toString(Evidence::Registered), "registered");
        QCOMPARE(toString(Evidence::ResponseValue), "response-value");
        QCOMPARE(toString(Evidence::Lead), "lead");

        QCOMPARE(toString(Refusal::InvalidUrl), "invalid-url");
        QCOMPARE(toString(Refusal::SchemeNotHttps), "scheme-not-https");
        QCOMPARE(toString(Refusal::NonDefaultPort), "non-default-port");
        QCOMPARE(toString(Refusal::UserInfoPresent), "userinfo-present");
        QCOMPARE(toString(Refusal::HostUnknown), "host-unknown");
        QCOMPARE(toString(Refusal::TierNotPermitted), "tier-not-permitted");
        QCOMPARE(toString(Refusal::ExperimentalRefused), "experimental-refused");
        QCOMPARE(toString(Refusal::EvidenceTooWeak), "evidence-too-weak");

        // Uniqueness across each enum, so a new value cannot silently reuse a name.
        QCOMPARE(QSet<QString>({QString::fromLatin1(toString(Tier::Production)),
                                 QString::fromLatin1(toString(Tier::Staging)),
                                 QString::fromLatin1(toString(Tier::Experimental))})
                         .size(),
                 3);
        QCOMPARE(QSet<QString>({QString::fromLatin1(toString(Refusal::InvalidUrl)),
                                 QString::fromLatin1(toString(Refusal::SchemeNotHttps)),
                                 QString::fromLatin1(toString(Refusal::NonDefaultPort)),
                                 QString::fromLatin1(toString(Refusal::UserInfoPresent)),
                                 QString::fromLatin1(toString(Refusal::HostUnknown)),
                                 QString::fromLatin1(toString(Refusal::TierNotPermitted)),
                                 QString::fromLatin1(toString(Refusal::ExperimentalRefused)),
                                 QString::fromLatin1(toString(Refusal::EvidenceTooWeak))})
                         .size(),
                 8);
    }

    // ── findRule edge cases ─────────────────────────────────────────────────

    void findRuleMatchesCaseInsensitively()
    {
        // The one place the ASCII fold is exercised for real: findRule takes a
        // std::string_view, so it gets none of QUrl's parsing-time normalisation.
        // Measured on Qt 6.11.1, QUrl lowercases the host itself, which means a
        // case test routed through classifyRefusal would prove nothing about this
        // helper. DNS is case-insensitive, so a host written in any case is the
        // same host and must resolve to the same row.
        const HostRule* lower = findRule("studio-api-prod.suno.com");
        const HostRule* upper = findRule("STUDIO-API-PROD.SUNO.COM");
        const HostRule* mixed = findRule("Studio-Api-Prod.Suno.Com");
        QVERIFY(lower != nullptr);
        QVERIFY(upper != nullptr);
        QVERIFY(mixed != nullptr);
        QVERIFY(sameRow(*lower, *upper));
        QVERIFY(sameRow(*lower, *mixed));
        QCOMPARE(upper, lower);
        QCOMPARE(mixed, lower);
    }

    void findRuleResolvesTheUploadOriginAndReturnsNullForUnknownHosts()
    {
        const auto upload = ruleFor("suno-uploads.s3.amazonaws.com");
        QVERIFY(upload.has_value());
        QCOMPARE(upload->role, Role::UploadOrigin);
        QCOMPARE(upload->tier, Tier::Production);
        QCOMPARE(upload->evidence, Evidence::Captured);

        // Null is the fail-closed answer for an unverified host, not an error.
        QVERIFY(findRule("api.suno.ai") == nullptr);
        QVERIFY(findRule("") == nullptr);
        QVERIFY(findRule("suno.com.") == nullptr);
        // A prefix or a subdomain of a registered host is a different host. There
        // is no wildcard in this registry and one must not appear: "any host under
        // suno.com" would hand the bearer to every host Suno ever points a CDN at.
        QVERIFY(findRule("evil.studio-api-prod.suno.com") == nullptr);
        QVERIFY(findRule("studio-api-prod.suno.com.evil.example.com") == nullptr);
    }
};

#include "test_CapturedHosts.moc"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestCapturedHosts test;
    return QTest::qExec(&test, argc, argv);
}
