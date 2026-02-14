/**
 * @file HelpPrinter.hpp
 * @brief Help and version text printing.
 *
 * Single responsibility: Format and print help/version text to stdout.
 */

#pragma once

namespace vc {

class HelpPrinter {
public:
    static void printVersion();
    static void printHelp();
};

} // namespace vc
