#pragma once
// SunoDatabase.hpp - Database for storing Suno song library
// Uses SQLite for persistent storage of song metadata and lyrics

#include <QSqlDatabase>
#include <optional>
#include <string>
#include <vector>
#include "SunoModels.hpp"
#include "util/Result.hpp"

namespace vc::suno {

class SunoDatabase {
public:
    SunoDatabase();
    ~SunoDatabase();

    Result<void> init(const std::string& dbPath);

    /// The file this connection was opened on (empty before init). One owner of
    /// "where the library lives": the lineage store opens its own connection to the
    /// same file rather than repeating the `dataDir() / "suno_library.db"` rule.
    [[nodiscard]] const std::string& path() const { return path_; }

    Result<void> saveClip(const SunoClip& clip);
    Result<void> saveClips(const std::vector<SunoClip>& clips);

    Result<std::vector<SunoClip>> getAllClips();
    Result<std::optional<SunoClip>> getClip(const std::string& id);

    Result<void> saveAlignedLyrics(const std::string& clipId,
                                   const std::string& alignedLyricsJson);
    Result<std::string> getAlignedLyrics(const std::string& clipId);
    bool hasLyrics(const std::string& clipId) const;

    // Search functionality
    Result<std::vector<SunoClip>> searchClips(const std::string& query);

private:
    QSqlDatabase db_;
    std::string path_;
    bool initialized_{false};
};

} // namespace vc::suno
