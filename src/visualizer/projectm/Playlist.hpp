#pragma once
/**
 * @file Playlist.hpp
 * @brief native projectM v4 playlist wrapper.
 */

#include "projectM-4/playlist.h"
#include <filesystem>
#include <string>
#include <vector>
#include "util/Signal.hpp"
#include "util/Types.hpp"

namespace fs = std::filesystem;

namespace vc::pm {

/**
 * @brief Wraps the projectm_playlist_handle.
 */
class Playlist {
public:
    Playlist();
    ~Playlist();

    // Non-copyable
    Playlist(const Playlist&) = delete;
    Playlist& operator=(const Playlist&) = delete;

    /**
     * @brief Initialize the playlist for a given engine handle.
     */
    bool init(projectm_handle engine);

    /**
     * @brief Clean up the playlist.
     */
    void shutdown();

    /**
     * @brief Clear all items from the playlist.
     */
    void clear();

    /**
     * @brief Add a path (file or directory) to the playlist.
     */
    u32 addPath(const std::string& path, bool recursive = true);

    /**
     * @brief Sort the playlist.
     */
    void sort();

    /**
     * @brief Set whether to shuffle.
     */
    void setShuffle(bool enabled);

    /**
     * @brief Play the next preset.
     */
    void next(bool immediate = false);

    /**
     * @brief Play the previous preset.
     */
    void previous(bool immediate = false);

    /**
     * @brief Set the current position in the playlist.
     */
    void setPosition(u32 index, bool immediate = false);

    /**
     * @brief Get the number of items in the playlist.
     */
    u32 size() const;

    /**
     * @brief Get the item path at a given index.
     */
    std::string itemAt(u32 index) const;

    /**
     * @brief Get the underlying handle.
     */
    projectm_playlist_handle handle() {
        return handle_;
    }

    /**
     * @brief Signal emitted when the preset is switched by the native playlist.
     * Params: (is_hard_cut, index)
     */
    Signal<bool, u32> switched;

private:
    static void onSwitched(bool is_hard_cut,
                           unsigned int index,
                           void* user_data);

    projectm_playlist_handle handle_{nullptr};
};

} // namespace vc::pm
