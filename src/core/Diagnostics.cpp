#include "Diagnostics.hpp"
#include "BuildConfig.hpp"
#include "CliUtils.hpp"

#include <QtGlobal>
#include <QSysInfo>

#include <fstream>
#include <string>

namespace vc {
namespace {

const char* compilerName() noexcept {
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#elif defined(_MSC_VER)
    return "msvc";
#else
    return "unknown";
#endif
}

} // namespace

DiagnosticsReport collectDiagnosticsReport() {
    // The COMPLETE allowlist. Each value is a fact about the build or the Qt
    // runtime. Nothing here is read from Config, CredentialStore, SunoClient,
    // the environment, argv (other than through this module's own API), or any
    // path the user chose. Do not add a loop over a map here: the point of the
    // allowlist is that every key is visible on this screen.
    return DiagnosticsReport{
        {"application_version", std::string(Cli::version())},
        {"qt_version", std::string(qVersion())},
        {"os", QSysInfo::productType().toStdString()},
        {"cpu_architecture", QSysInfo::currentCpuArchitecture().toStdString()},
        {"compiler", compilerName()},
        {"feature_avfilter", CHADVIS_HAS_AVFILTER ? "on" : "off"},
        {"feature_avaes", CHADVIS_HAVE_AVAES_INIT ? "on" : "off"},
    };
}

std::string renderDiagnosticsReport(const DiagnosticsReport& report) {
    std::string out = "ChadVis diagnostics\n";
    for (const auto& [key, value] : report) {
        out += key;
        out += ": ";
        out += value;
        out += '\n';
    }
    return out;
}

Result<void> writeDiagnosticsReport(const fs::path& dest,
                                    const DiagnosticsReport& report) {
    if (dest.empty()) {
        return Result<void>::err("empty diagnostics output path");
    }
    std::error_code ec;
    const fs::path parent = dest.parent_path();
    if (!parent.empty() && !fs::exists(parent, ec)) {
        return Result<void>::err("diagnostics output directory does not exist: " +
                                 parent.string());
    }
    if (ec) {
        return Result<void>::err("cannot inspect diagnostics output directory: " +
                                 ec.message());
    }

    const std::string bytes = renderDiagnosticsReport(report);
    std::ofstream file(dest, std::ios::binary | std::ios::trunc);
    if (!file) {
        return Result<void>::err("could not open diagnostics output: " + dest.string());
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!static_cast<bool>(file)) {
        return Result<void>::err("failed while writing diagnostics output: " +
                                 dest.string());
    }
    return Result<void>::ok();
}

} // namespace vc
