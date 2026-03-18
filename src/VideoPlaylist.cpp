#include "VideoPlaylist.h"

#include <algorithm>
#include <array>
#include <chrono>

using namespace std::string_literals;

VideoPlaylist::VideoPlaylist(std::filesystem::path directory)
    : directory_(std::move(directory)),
      rng_(static_cast<std::mt19937::result_type>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

void VideoPlaylist::Refresh() {
    files_.clear();

    static constexpr std::array kExtensions = {
        L".mov",
        L".mp4",
        L".m4v",
        L".mkv"
    };

    if (!std::filesystem::exists(directory_) || !std::filesystem::is_directory(directory_)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        auto ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end()) {
            files_.push_back(entry.path());
        }
    }
}

bool VideoPlaylist::Empty() const noexcept {
    return files_.empty();
}

const std::vector<std::filesystem::path>& VideoPlaylist::Files() const noexcept {
    return files_;
}

std::optional<std::filesystem::path> VideoPlaylist::NextRandom() {
    if (files_.empty()) {
        return std::nullopt;
    }

    std::uniform_int_distribution<std::size_t> dist(0, files_.size() - 1);
    return files_[dist(rng_)];
}
