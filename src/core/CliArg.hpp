/**
 * @file CliArg.hpp
 * @brief Declarative CLI argument descriptor types.
 *
 * Defines the CliArgType enum and CliArg struct for table-driven CLI
 * argument parsing. The actual argument table lives in CliArgs.inc,
 * which is included with different macro definitions for different
 * purposes (parsing, help generation, completion, etc.).
 *
 * @section Patterns
 * - X-Macro: CliArgs.inc is the single source of truth for all flags.
 * - Table-Driven: Eliminates repetitive if/else chains in parseArgs().
 *
 * @note The CLI_BOOL/CLI_INT/CLI_FLOAT/CLI_STRING/CLI_PATH tokens are
 *       defined as MACRO NAMES in CliArgs.inc (not here). Each include
 *       site defines them differently for the specific use case.
 */

#pragma once
#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include "util/Types.hpp"

namespace vc {

/// CLI argument value types — for runtime type queries (e.g., help generation)
enum class CliArgType {
	Bool,   ///< --flag / --no-flag (sets true/false)
	Int,    ///< --flag <int>
	Float,  ///< --flag <float>
	String, ///< --flag <string>
	Path,   ///< --flag <path>
};

/// Declarative CLI argument descriptor (for future use: help generation, completion)
struct CliArg {
	std::string_view longName;       ///< e.g. "--visualizer-fps"
	std::string_view shortName;      ///< e.g. "-p" or "" if none
	CliArgType type;                 ///< Value type
	std::string_view helpText;       ///< e.g. "Target frame rate"
	std::string_view helpDefault;    ///< e.g. "60" or "" if none
	std::string_view negationName;   ///< e.g. "--no-visualizer-shuffle" or ""
};

} // namespace vc
