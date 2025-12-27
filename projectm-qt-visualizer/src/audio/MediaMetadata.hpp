#pragma once
// MediaMetadata.hpp - Audio file metadata extraction
// TagLib wrapper because reading ID3 tags manually is pain

#include "util/Types.hpp"
#include "util/Result.hpp"
#include <QPixmap>

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
    std::optional<QPixmap> albumArt;
    
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
    
private:
    static std::optional<QPixmap> extractAlbumArt(const fs::path& path);
};

} // namespace vc
