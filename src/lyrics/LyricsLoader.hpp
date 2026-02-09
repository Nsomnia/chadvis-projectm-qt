/**
 * @file LyricsLoader.hpp
 * @brief Unified interface for loading lyrics from any source.
 *
 * Async lyrics loading with caching support. Handles Suno API,
 * local files (SRT, LRC), and database storage.
 *
 * Pattern: Strategy + Async Loader
 *
 * @author ChadVis Agent
 * @version 11.0 (Maximum Effort Edition)
 */

#pragma once
#include <functional>
#include <future>
#include <optional>
#include <string>
#include "LyricsData.hpp"
#include "util/Result.hpp"

namespace vc {

// Forward declarations
namespace suno {
class SunoClient;
class SunoDatabase;
}

/**
 * @brief Loading source types
 */
enum class LyricsSource {
    SunoApi,        ///< Fetch from Suno API
    Database,       ///< Load from local SQLite
    SrtFile,        ///< Load SRT subtitle file
    LrcFile,        ///< Load LRC lyrics file
    TextFile,       ///< Plain text (no timing)
    Cache           ///< Memory cache
};

/**
 * @brief Loading options
 */
struct LyricsLoadOptions {
    bool useCache{true};            ///< Check memory cache first
    bool fallbackToUnsynced{true};  ///< Return plain text if sync fails
    bool autoFetchMissing{true};    ///< Fetch from API if not in database
    int retryCount{3};              ///< Number of retries for API calls
    int retryDelayMs{1000};         ///< Delay between retries
};

/**
 * @brief Result of lyrics loading operation
 */
struct LyricsLoadResult {
    LyricsData data;
    LyricsSource source;
    bool fromCache{false};
    std::string errorMessage;
    bool success{false};
    
    bool isOk() const { return success; }
};

/**
 * @brief Callback for async loading
 */
using LyricsLoadCallback = std::function<void(const LyricsLoadResult& result)>;

/**
 * @brief Unified lyrics loader
 *
 * This class provides a single interface for loading lyrics from
 * any source. It handles caching, async operations, and fallbacks.
 *
 * Usage:
 * @code
 * LyricsLoader loader(sunoClient, database);
 * 
 * // Async load
 * loader.loadAsync("song-id", LyricsSource::Auto, [](auto result) {
 *     if (result.isOk()) displayLyrics(result.data);
 * });
 * 
 * // Sync load
 * auto result = loader.loadSync("song-id");
 * @endcode
 */
class LyricsLoader {
public:
    /**
     * @brief Construct with required dependencies
     */
    LyricsLoader(suno::SunoClient* client, suno::SunoDatabase* db);
    ~LyricsLoader();
    
    /**
     * @brief Load lyrics asynchronously
     * @param songId Unique song identifier
     * @param source Preferred source (Auto tries all in order)
     * @param callback Called when loading completes
     * @param options Loading options
     */
    void loadAsync(const std::string& songId,
                   LyricsSource source,
                   LyricsLoadCallback callback,
                   const LyricsLoadOptions& options = {});
    
    /**
     * @brief Load lyrics synchronously (blocks until complete)
     * @return Load result with data or error
     */
    LyricsLoadResult loadSync(const std::string& songId,
                              LyricsSource source = LyricsSource::Database,
                              const LyricsLoadOptions& options = {});
    
    /**
     * @brief Load from specific file path
     */
    LyricsLoadResult loadFromFile(const fs::path& path);
    
    /**
     * @brief Preload lyrics for upcoming songs (background)
     */
    void preload(const std::vector<std::string>& songIds);
    
    /**
     * @brief Clear memory cache
     */
    void clearCache();
    
    /**
     * @brief Check if lyrics are in cache
     */
    bool isCached(const std::string& songId) const;
    
    /**
     * @brief Get cache statistics
     */
    struct CacheStats {
        size_t hits{0};
        size_t misses{0};
        size_t size{0};
    };
    CacheStats getCacheStats() const;
    
    /**
     * @brief Set default loading options
     */
    void setDefaultOptions(const LyricsLoadOptions& options);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Auto-detect source based on song ID and availability
 *
 * Tries: Cache -> Database -> Suno API -> File (if local)
 */
class LyricsSourceDetector {
public:
    /**
     * @brief Determine best source for a song
     */
    static LyricsSource detect(const std::string& songId,
                               suno::SunoDatabase* db,
                               LyricsLoader* loader);
    
    /**
     * @brief Check if ID looks like a Suno UUID
     */
    static bool isSunoId(const std::string& id);
    
    /**
     * @brief Check if file exists with lyrics extension
     */
    static std::optional<fs::path> findLyricsFile(const fs::path& audioPath);
};

} // namespace vc
