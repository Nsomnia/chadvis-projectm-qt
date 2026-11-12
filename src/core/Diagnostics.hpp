#pragma once
// Diagnostics.hpp -- --diagnostics: an offline, allowlisted support report (T0130).
//
// SECURITY CONTRACT (AGENTS.md §11). This report contains ONLY the keys written
// literally in collectDiagnosticsReport() below. It is an ALLOWLIST, not a
// deny-list: there is no loop over config.toml, the environment,
// CredentialStore, account state, network replies, clip ids, media URLs or
// file paths that could smuggle a secret into the output. Adding a key here is
// a deliberate review decision, not a data-dump default. Nothing is uploaded;
// writeDiagnosticsReport() writes only to the path the user chose on the
// command line, and the destination path itself is never part of the report.

#include "util/Result.hpp"
#include "util/Types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace vc {

using DiagnosticsEntry = std::pair<std::string, std::string>;
using DiagnosticsReport = std::vector<DiagnosticsEntry>;

/// Build the report from the explicit allowlist. Every value is a compile-time
/// or Qt-runtime fact about the BUILD, never about the user, their account,
/// their library, or their machine's paths.
[[nodiscard]] DiagnosticsReport collectDiagnosticsReport();

/// Deterministic rendering: one `key: value` line per entry, no timestamps and
/// no random data, so the file is diffable and a support bundle is stable.
[[nodiscard]] std::string renderDiagnosticsReport(const DiagnosticsReport& report);

/// Write the rendered report to the user-chosen path. Expected failures (bad
/// path, unwritable file, short write) come back as Result errors rather than
/// being silently swallowed (AGENTS.md §4). Never writes anywhere else.
[[nodiscard]] Result<void> writeDiagnosticsReport(const fs::path& dest,
                                                  const DiagnosticsReport& report);

} // namespace vc
