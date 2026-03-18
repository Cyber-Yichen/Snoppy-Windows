#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    FFmpegDecoder(const FFmpegDecoder&) = delete;
    FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;

    bool Open(const std::filesystem::path& file);
    void Close();

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] int Width() const noexcept;
    [[nodiscard]] int Height() const noexcept;
    [[nodiscard]] double Fps() const noexcept;

    bool ReadFrameBGRA(std::vector<std::uint8_t>& buffer, int& width, int& height, bool& endOfStream);
    bool SeekToStart();

    [[nodiscard]] std::wstring LastError() const noexcept;

private:
    bool InitScaler();
    bool DrainDecoder(std::vector<std::uint8_t>& buffer, int& width, int& height, bool& producedFrame);
    void SetError(const std::wstring& text);

private:
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* bgraFrame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* swsCtx_ = nullptr;

    int videoStreamIndex_ = -1;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
    std::wstring lastError_;
    std::vector<std::uint8_t> frameBuffer_;
};
