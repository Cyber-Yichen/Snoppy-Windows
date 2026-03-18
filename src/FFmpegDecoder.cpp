#include "FFmpegDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

#include <Windows.h>

#include <algorithm>
#include <format>

namespace {
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), result.data(), size);
    return result;
}

std::wstring AvErrorToWString(int errnum) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, errnum);
    return Utf8ToWide(buffer);
}
}

FFmpegDecoder::FFmpegDecoder() {
    frame_ = av_frame_alloc();
    bgraFrame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
}

FFmpegDecoder::~FFmpegDecoder() {
    Close();
    if (packet_) {
        av_packet_free(&packet_);
    }
    if (bgraFrame_) {
        av_frame_free(&bgraFrame_);
    }
    if (frame_) {
        av_frame_free(&frame_);
    }
}

bool FFmpegDecoder::Open(const std::filesystem::path& file) {
    Close();

    const auto pathUtf8 = file.u8string();
    int rc = avformat_open_input(&formatCtx_, reinterpret_cast<const char*>(pathUtf8.c_str()), nullptr, nullptr);
    if (rc < 0) {
        SetError(L"avformat_open_input failed: " + AvErrorToWString(rc));
        return false;
    }

    rc = avformat_find_stream_info(formatCtx_, nullptr);
    if (rc < 0) {
        SetError(L"avformat_find_stream_info failed: " + AvErrorToWString(rc));
        return false;
    }

    rc = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (rc < 0) {
        SetError(L"No video stream found: " + AvErrorToWString(rc));
        return false;
    }
    videoStreamIndex_ = rc;

    AVStream* stream = formatCtx_->streams[videoStreamIndex_];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        SetError(L"Unsupported codec.");
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        SetError(L"avcodec_alloc_context3 failed.");
        return false;
    }

    rc = avcodec_parameters_to_context(codecCtx_, stream->codecpar);
    if (rc < 0) {
        SetError(L"avcodec_parameters_to_context failed: " + AvErrorToWString(rc));
        return false;
    }

    rc = avcodec_open2(codecCtx_, codec, nullptr);
    if (rc < 0) {
        SetError(L"avcodec_open2 failed: " + AvErrorToWString(rc));
        return false;
    }

    width_ = codecCtx_->width;
    height_ = codecCtx_->height;

    AVRational rate = av_guess_frame_rate(formatCtx_, stream, nullptr);
    if (rate.num > 0 && rate.den > 0) {
        fps_ = av_q2d(rate);
    } else {
        fps_ = 30.0;
    }
    if (fps_ < 1.0 || fps_ > 240.0) {
        fps_ = 30.0;
    }

    if (!InitScaler()) {
        return false;
    }

    lastError_.clear();
    return true;
}

void FFmpegDecoder::Close() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    frameBuffer_.clear();

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
    }

    videoStreamIndex_ = -1;
    width_ = 0;
    height_ = 0;
    fps_ = 30.0;
}

bool FFmpegDecoder::IsOpen() const noexcept {
    return formatCtx_ != nullptr && codecCtx_ != nullptr && videoStreamIndex_ >= 0;
}

int FFmpegDecoder::Width() const noexcept { return width_; }
int FFmpegDecoder::Height() const noexcept { return height_; }
double FFmpegDecoder::Fps() const noexcept { return fps_; }
std::wstring FFmpegDecoder::LastError() const noexcept { return lastError_; }

bool FFmpegDecoder::InitScaler() {
    swsCtx_ = sws_getContext(
        width_, height_, codecCtx_->pix_fmt,
        width_, height_, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!swsCtx_) {
        SetError(L"sws_getContext failed.");
        return false;
    }

    const int bytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, width_, height_, 1);
    if (bytes <= 0) {
        SetError(L"av_image_get_buffer_size failed.");
        return false;
    }
    frameBuffer_.resize(static_cast<std::size_t>(bytes));

    int rc = av_image_fill_arrays(
        bgraFrame_->data,
        bgraFrame_->linesize,
        frameBuffer_.data(),
        AV_PIX_FMT_BGRA,
        width_,
        height_,
        1);

    if (rc < 0) {
        SetError(L"av_image_fill_arrays failed: " + AvErrorToWString(rc));
        return false;
    }

    return true;
}

bool FFmpegDecoder::DrainDecoder(std::vector<std::uint8_t>& buffer, int& width, int& height, bool& producedFrame) {
    producedFrame = false;

    const int rc = avcodec_receive_frame(codecCtx_, frame_);
    if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
        return true;
    }
    if (rc < 0) {
        SetError(L"avcodec_receive_frame failed: " + AvErrorToWString(rc));
        return false;
    }

    sws_scale(
        swsCtx_,
        frame_->data,
        frame_->linesize,
        0,
        height_,
        bgraFrame_->data,
        bgraFrame_->linesize);

    buffer = frameBuffer_;

    for (std::size_t i = 0; i + 3 < buffer.size(); i += 4) {
        const auto a = static_cast<std::uint32_t>(buffer[i + 3]);
        buffer[i + 0] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(buffer[i + 0]) * a) / 255);
        buffer[i + 1] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(buffer[i + 1]) * a) / 255);
        buffer[i + 2] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(buffer[i + 2]) * a) / 255);
    }

    width = width_;
    height = height_;
    producedFrame = true;
    return true;
}

bool FFmpegDecoder::ReadFrameBGRA(std::vector<std::uint8_t>& buffer, int& width, int& height, bool& endOfStream) {
    endOfStream = false;
    width = 0;
    height = 0;

    if (!IsOpen()) {
        SetError(L"Decoder is not open.");
        return false;
    }

    bool produced = false;
    while (true) {
        if (!DrainDecoder(buffer, width, height, produced)) {
            return false;
        }
        if (produced) {
            return true;
        }

        const int rc = av_read_frame(formatCtx_, packet_);
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(codecCtx_, nullptr);
            if (!DrainDecoder(buffer, width, height, produced)) {
                return false;
            }
            endOfStream = !produced;
            return produced || endOfStream;
        }
        if (rc < 0) {
            SetError(L"av_read_frame failed: " + AvErrorToWString(rc));
            return false;
        }

        if (packet_->stream_index == videoStreamIndex_) {
            const int sendRc = avcodec_send_packet(codecCtx_, packet_);
            av_packet_unref(packet_);
            if (sendRc < 0) {
                SetError(L"avcodec_send_packet failed: " + AvErrorToWString(sendRc));
                return false;
            }
        } else {
            av_packet_unref(packet_);
        }
    }
}

bool FFmpegDecoder::SeekToStart() {
    if (!IsOpen()) {
        return false;
    }

    int rc = av_seek_frame(formatCtx_, videoStreamIndex_, 0, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) {
        SetError(L"av_seek_frame failed: " + AvErrorToWString(rc));
        return false;
    }

    avcodec_flush_buffers(codecCtx_);
    return true;
}

void FFmpegDecoder::SetError(const std::wstring& text) {
    lastError_ = text;
}
