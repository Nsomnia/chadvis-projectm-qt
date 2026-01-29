/**
 * @file CliUtils.hpp
 * @brief Command-line interface utilities with color support.
 *
 * Provides color output, formatted help text, and smart error handling
 * for a premium CLI experience. Respects NO_COLOR environment variable.
 *
 * @author ChadVis Agent
 * @version 1337.0 (Rizz Mode Edition)
 */

#pragma once
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace vc {

/**
 * @brief ANSI color codes for terminal output
 */
namespace CliColor {
    // Check if colors should be used (respects NO_COLOR)
    bool shouldUseColor();
    
    // Reset
    inline const char* reset() { return shouldUseColor() ? "\033[0m" : ""; }
    
    // Standard colors
    inline const char* black()   { return shouldUseColor() ? "\033[30m" : ""; }
    inline const char* red()     { return shouldUseColor() ? "\033[31m" : ""; }
    inline const char* green()   { return shouldUseColor() ? "\033[32m" : ""; }
    inline const char* yellow()  { return shouldUseColor() ? "\033[33m" : ""; }
    inline const char* blue()    { return shouldUseColor() ? "\033[34m" : ""; }
    inline const char* magenta() { return shouldUseColor() ? "\033[35m" : ""; }
    inline const char* cyan()    { return shouldUseColor() ? "\033[36m" : ""; }
    inline const char* white()   { return shouldUseColor() ? "\033[37m" : ""; }
    
    // Bright colors
    inline const char* brightRed()     { return shouldUseColor() ? "\033[91m" : ""; }
    inline const char* brightGreen()   { return shouldUseColor() ? "\033[92m" : ""; }
    inline const char* brightYellow()  { return shouldUseColor() ? "\033[93m" : ""; }
    inline const char* brightBlue()    { return shouldUseColor() ? "\033[94m" : ""; }
    inline const char* brightMagenta() { return shouldUseColor() ? "\033[95m" : ""; }
    inline const char* brightCyan()    { return shouldUseColor() ? "\033[96m" : ""; }
    inline const char* brightWhite()   { return shouldUseColor() ? "\033[97m" : ""; }
    
    // Styles
    inline const char* bold()      { return shouldUseColor() ? "\033[1m" : ""; }
    inline const char* dim()       { return shouldUseColor() ? "\033[2m" : ""; }
    inline const char* italic()    { return shouldUseColor() ? "\033[3m" : ""; }
    inline const char* underline() { return shouldUseColor() ? "\033[4m" : ""; }
} // namespace CliColor

/**
 * @brief CLI output helpers
 */
namespace Cli {
    /**
     * @brief Print an error message with red highlighting
     */
    void printError(std::string_view message);
    
    /**
     * @brief Print a warning message with yellow highlighting
     */
    void printWarning(std::string_view message);
    
    /**
     * @brief Print a success message with green highlighting
     */
    void printSuccess(std::string_view message);
    
    /**
     * @brief Print an info message with blue highlighting
     */
    void printInfo(std::string_view message);
    
    /**
     * @brief Print a header/banner with styling
     */
    void printHeader(std::string_view title);
    
    /**
     * @brief Print a section header
     */
    void printSection(std::string_view title);
    
    /**
     * @brief Print an option with description
     */
    void printOption(std::string_view flags, std::string_view description, 
                     std::optional<std::string_view> defaultVal = std::nullopt);
    
    /**
     * @brief Print an unknown flag error with suggestions
     */
    void printUnknownFlagError(std::string_view flag, 
                               std::initializer_list<std::string_view> suggestions);
    
    /**
     * @brief Find closest matching flag for typo correction
     */
    std::optional<std::string> findClosestMatch(
        std::string_view input,
        std::initializer_list<std::string_view> candidates);
    
    /**
     * @brief Format a boolean value for display
     */
    inline std::string formatBool(bool value) {
        return value ? std::string(CliColor::green()) + "yes" + CliColor::reset()
                     : std::string(CliColor::red()) + "no" + CliColor::reset();
    }
    
    /**
     * @brief Format a file path for display
     */
    std::string formatPath(const std::filesystem::path& path);
    
    /**
     * @brief Generate shell completion script
     */
    void generateCompletionScript(std::string_view shell);
} // namespace Cli

/**
 * @brief Help topic system for detailed documentation
 */
class HelpSystem {
public:
    /**
     * @brief Available help topics
     */
    enum class Topic {
        General,
        Audio,
        Visualizer,
        Recording,
        Suno,
        Karaoke,
        Examples,
        Config,
        All
    };
    
    /**
     * @brief Print help for a specific topic
     */
    static void printHelp(Topic topic);
    
    /**
     * @brief Parse topic from string
     */
    static std::optional<Topic> parseTopic(std::string_view name);
    
    /**
     * @brief Get topic name
     */
    static std::string_view topicName(Topic topic);
    
    /**
     * @brief List all available topics
     */
    static void listTopics();
};

} // namespace vc
