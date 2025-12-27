#include "MediaMetadata.hpp"
#include "util/FileUtils.hpp"
#include "core/Logger.hpp"

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>

#include <QBuffer>

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
    
    // Try to extract album art
    meta.albumArt = extractAlbumArt(path);
    
    LOG_DEBUG("Read metadata for: {} - {}", meta.artist, meta.title);
    return Result<MediaMetadata>::ok(std::move(meta));
}

bool MetadataReader::canRead(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return file::audioExtensions.contains(ext);
}

std::optional<QPixmap> MetadataReader::extractAlbumArt(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // Try MPEG/ID3v2
    if (ext == ".mp3") {
        TagLib::MPEG::File mpegFile(path.c_str());
        if (mpegFile.isValid() && mpegFile.ID3v2Tag()) {
            auto* tag = mpegFile.ID3v2Tag();
            auto frames = tag->frameListMap()["APIC"];
            if (!frames.isEmpty()) {
                auto* pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
                if (pic) {
                    QPixmap pixmap;
                    if (pixmap.loadFromData(
                        reinterpret_cast<const uchar*>(pic->picture().data()),
                        pic->picture().size())) {
                        return pixmap;
                    }
                }
            }
        }
    }
    
    // Try FLAC
    if (ext == ".flac") {
        TagLib::FLAC::File flacFile(path.c_str());
        if (flacFile.isValid()) {
            auto pictures = flacFile.pictureList();
            if (!pictures.isEmpty()) {
                auto* pic = pictures.front();
                QPixmap pixmap;
                if (pixmap.loadFromData(
                    reinterpret_cast<const uchar*>(pic->data().data()),
                    pic->data().size())) {
                    return pixmap;
                }
            }
        }
    }
    
    return std::nullopt;
}

} // namespace vc
