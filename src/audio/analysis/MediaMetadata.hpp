#pragma once
// MediaMetadata.hpp - Audio file metadata extraction
// TagLib wrapper because reading ID3 tags manually is pain

#include "util/Types.hpp"
#include "util/Result.hpp"

namespace vc {

struct MediaMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    u32 year{0};
    u32 trackNumber{0};
    Duration duration{0};
    u32 bitrate{0};         // kbps
    u32 sampleRate{0};      // Hz
    u32 channels{0};
    std::string sunoClipId; // Optional: Link to Suno Clip

    // NOTE: this used to carry a `std::optional<QPixmap> albumArt`, written once
    // by a `QPixmap::loadFromData` decode per file and read by nobody. Beyond
    // the wasted GUI-thread decode, a QPixmap must be *created and destroyed* on
    // the thread that owns its QGuiApplication, and MediaMetadata is embedded by
    // value in PlaylistItem — so the field made the whole Playlist type
    // un-movable across a thread boundary for the sake of an unread byte buffer.
    // If art is ever wanted, the correct shape is the raw picture bytes (or a
    // `QImage`, which is shareable across threads) kept here, decoded at the
    // paint site. Do not reintroduce a QPixmap.

    // Formatted display strings
    std::string displayTitle() const;
    std::string displayArtist() const;
    std::string displayAlbum() const;
    std::string formatLine(const std::string& format) const;
};

class MetadataReader {
public:
    static Result<MediaMetadata> read(const fs::path& path);
    static bool canRead(const fs::path& path);
};

} // namespace vc
