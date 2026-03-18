#pragma once

#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

class VideoPlaylist {
public:
    explicit VideoPlaylist(std::filesystem::path directory);

    void Refresh();
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] const std::vector<std::filesystem::path>& Files() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> NextRandom();

private:
    std::filesystem::path directory_;
    std::vector<std::filesystem::path> files_;
    std::mt19937 rng_;
};
