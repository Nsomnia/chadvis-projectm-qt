#include "MediaMetadata.hpp"
#include "util/FileUtils.hpp"
#include "core/Logger.hpp"

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>

#include <algorithm>

namespace vc {

std::string MediaMetadata::displayTitle() const {
    if (!title.empty()) return title;
    return "Unknown Title";
}

std::string MediaMetadata::displayArtist() const {
    if (!artist.empty()) return artist;
    return "Unknown Artist";
}

std::string MediaMetadata::displayAlbum() const {
    if (!album.empty()) return album;
    return "Unknown Album";
}

std::string MediaMetadata::formatLine(const std::string& format) const {
    std::string result = format;
    
    // Simple placeholder replacement
    auto replace = [&result](const std::string& placeholder, const std::string& value) {
        size_t pos;
        while ((pos = result.find(placeholder)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
        }
    };
    
    replace("{title}", displayTitle());
    replace("{artist}", displayArtist());
    replace("{album}", displayAlbum());
    replace("{genre}", genre.empty() ? "Unknown" : genre);
    replace("{year}", year > 0 ? std::to_string(year) : "");
    replace("{track}", trackNumber > 0 ? std::to_string(trackNumber) : "");
    replace("{duration}", file::formatDuration(duration));
    replace("{bitrate}", std::to_string(bitrate) + " kbps");
    
    return result;
}

Result<MediaMetadata> MetadataReader::read(const fs::path& path) {
    MediaMetadata meta;
    
    TagLib::FileRef file(path.c_str());
    if (file.isNull()) {
        return Result<MediaMetadata>::err("Failed to open file: " + path.string());
    }
    
    if (file.tag()) {
        TagLib::Tag* tag = file.tag();
        meta.title = tag->title().to8Bit(true);
        meta.artist = tag->artist().to8Bit(true);
        meta.album = tag->album().to8Bit(true);
        meta.genre = tag->genre().to8Bit(true);
        meta.year = tag->year();
        meta.trackNumber = tag->track();
        
        std::string comment = tag->comment().to8Bit(true);
        if (comment.find("made by suno") != std::string::npos) {
            size_t idPos = comment.find("id=");
            if (idPos != std::string::npos) {
                meta.sunoClipId = comment.substr(idPos + 3);
                size_t endPos = meta.sunoClipId.find_first_of("; \r\n");
                if (endPos != std::string::npos) {
                    meta.sunoClipId = meta.sunoClipId.substr(0, endPos);
                }
            }
        }
    }
    
    if (file.audioProperties()) {
        TagLib::AudioProperties* props = file.audioProperties();
        meta.duration = Duration(props->lengthInMilliseconds());
        meta.bitrate = props->bitrate();
        meta.sampleRate = props->sampleRate();
        meta.channels = props->channels();
    }
    
    // If title is empty, use filename
    if (meta.title.empty()) {
        meta.title = path.stem().string();
    }

    LOG_DEBUG("Read metadata for: {} - {}", meta.artist, meta.title);
    return Result<MediaMetadata>::ok(std::move(meta));
}

bool MetadataReader::canRead(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return file::audioExtensions.contains(ext);
}

} // namespace vc
