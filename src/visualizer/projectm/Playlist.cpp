#include "Playlist.hpp"
#include "core/Logger.hpp"

namespace vc::pm {

void Playlist::onSwitched(bool is_hard_cut,
                          unsigned int index,
                          void* user_data) {
    auto* self = static_cast<Playlist*>(user_data);
    if (self) {
        self->switched.emitSignal(is_hard_cut, static_cast<u32>(index));
    }
}

Playlist::Playlist() = default;

Playlist::~Playlist() {
    shutdown();
}

bool Playlist::init(projectm_handle engine) {
    if (handle_)
        shutdown();

    handle_ = projectm_playlist_create(engine);
    if (!handle_) {
        LOG_ERROR("Playlist: Failed to create projectM playlist");
        return false;
    }

    projectm_playlist_set_preset_switched_event_callback(
            handle_, &Playlist::onSwitched, this);
    return true;
}

void Playlist::shutdown() {
    if (handle_) {
        projectm_playlist_destroy(handle_);
        handle_ = nullptr;
    }
}

void Playlist::clear() {
    if (handle_)
        projectm_playlist_clear(handle_);
}

u32 Playlist::addPath(const std::string& path, bool recursive) {
    if (!handle_)
        return 0;
    return projectm_playlist_add_path(handle_, path.c_str(), recursive, false);
}

void Playlist::sort() {
    if (!handle_)
        return;
    u32 count = size();
    if (count > 0) {
        projectm_playlist_sort(handle_,
                               0,
                               count,
                               SORT_PREDICATE_FULL_PATH,
                               SORT_ORDER_ASCENDING);
    }
}

void Playlist::setShuffle(bool enabled) {
    if (handle_)
        projectm_playlist_set_shuffle(handle_, enabled);
}

void Playlist::next(bool immediate) {
    if (handle_)
        projectm_playlist_play_next(handle_, immediate);
}

void Playlist::previous(bool immediate) {
    if (handle_)
        projectm_playlist_play_previous(handle_, immediate);
}

void Playlist::setPosition(u32 index, bool immediate) {
    if (handle_)
        projectm_playlist_set_position(handle_, index, immediate);
}

u32 Playlist::size() const {
    if (!handle_)
        return 0;
    return projectm_playlist_size(handle_);
}

std::string Playlist::itemAt(u32 index) const {
    if (!handle_)
        return "";
    char* path = projectm_playlist_item(handle_, index);
    if (!path)
        return "";
    std::string result(path);
    projectm_playlist_free_string(path);
    return result;
}

} // namespace vc::pm
