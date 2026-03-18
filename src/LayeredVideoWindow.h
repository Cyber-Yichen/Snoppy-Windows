#pragma once

#include "FFmpegDecoder.h"
#include "VideoPlaylist.h"

#include <Windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class LayeredVideoWindow {
public:
    struct Options {
        HWND parent = nullptr;
        RECT bounds{};
        bool previewMode = false;
        std::filesystem::path videoDirectory;
    };

    explicit LayeredVideoWindow(Options options);
    ~LayeredVideoWindow();

    LayeredVideoWindow(const LayeredVideoWindow&) = delete;
    LayeredVideoWindow& operator=(const LayeredVideoWindow&) = delete;

    bool Create(HINSTANCE instance);
    void Show(int cmdShow = SW_SHOW);
    void Close();
    [[nodiscard]] HWND Hwnd() const noexcept;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool OpenNextVideo();
    void StartPlaybackTimer();
    void StopPlaybackTimer();
    void OnPlaybackTick();
    void PaintPreviewFallback(HDC hdc);
    void BlitLayeredFrame(const std::vector<std::uint8_t>& bgra, int width, int height);

private:
    Options options_;
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    UINT_PTR timerId_ = 1;
    std::unique_ptr<VideoPlaylist> playlist_;
    FFmpegDecoder decoder_;
    std::filesystem::path currentVideo_;
    std::wstring statusText_;
};
