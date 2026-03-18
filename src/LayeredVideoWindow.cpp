#include "LayeredVideoWindow.h"

#include <Windowsx.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace {
constexpr wchar_t kWindowClassName[] = L"SnoppyLayeredVideoWindow";
}

LayeredVideoWindow::LayeredVideoWindow(Options options)
    : options_(std::move(options)) {
    playlist_ = std::make_unique<VideoPlaylist>(options_.videoDirectory);
    playlist_->Refresh();
}

LayeredVideoWindow::~LayeredVideoWindow() {
    Close();
}

bool LayeredVideoWindow::Create(HINSTANCE instance) {
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &LayeredVideoWindow::WindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&wc);

    DWORD style = options_.previewMode ? WS_CHILD | WS_VISIBLE : WS_POPUP | WS_VISIBLE;
    DWORD exStyle = options_.previewMode ? WS_EX_NOPARENTNOTIFY : WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

    const RECT& r = options_.bounds;
    hwnd_ = CreateWindowExW(
        exStyle,
        kWindowClassName,
        L"Snoppy",
        style,
        r.left,
        r.top,
        r.right - r.left,
        r.bottom - r.top,
        options_.parent,
        nullptr,
        instance_,
        this);

    return hwnd_ != nullptr;
}

void LayeredVideoWindow::Show(int cmdShow) {
    if (!hwnd_) return;
    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
}

void LayeredVideoWindow::Close() {
    StopPlaybackTimer();
    decoder_.Close();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

HWND LayeredVideoWindow::Hwnd() const noexcept {
    return hwnd_;
}

LRESULT CALLBACK LayeredVideoWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LayeredVideoWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<LayeredVideoWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<LayeredVideoWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT LayeredVideoWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        if (!OpenNextVideo()) {
            statusText_ = L"No playable video found in ./video";
        }
        StartPlaybackTimer();
        return 0;

    case WM_TIMER:
        if (wParam == timerId_) {
            OnPlaybackTick();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        PaintPreviewFallback(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_DESTROY:
        StopPlaybackTimer();
        return 0;
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

bool LayeredVideoWindow::OpenNextVideo() {
    if (!playlist_) {
        return false;
    }

    if (playlist_->Empty()) {
        playlist_->Refresh();
    }
    auto next = playlist_->NextRandom();
    if (!next.has_value()) {
        statusText_ = L"No videos found.";
        return false;
    }

    currentVideo_ = *next;
    if (!decoder_.Open(currentVideo_)) {
        statusText_ = std::format(L"Failed to open: {}\n{}", currentVideo_.filename().wstring(), decoder_.LastError());
        return false;
    }

    statusText_ = currentVideo_.filename().wstring();
    return true;
}

void LayeredVideoWindow::StartPlaybackTimer() {
    UINT fps = 30;
    if (decoder_.IsOpen()) {
        fps = static_cast<UINT>(std::clamp(decoder_.Fps(), 12.0, 60.0));
    }
    const UINT interval = std::max(15u, 1000u / fps);
    SetTimer(hwnd_, timerId_, interval, nullptr);
}

void LayeredVideoWindow::StopPlaybackTimer() {
    if (hwnd_) {
        KillTimer(hwnd_, timerId_);
    }
}

void LayeredVideoWindow::OnPlaybackTick() {
    if (!decoder_.IsOpen()) {
        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }

    std::vector<std::uint8_t> frame;
    int width = 0;
    int height = 0;
    bool eos = false;

    if (!decoder_.ReadFrameBGRA(frame, width, height, eos)) {
        statusText_ = decoder_.LastError();
        decoder_.Close();
        OpenNextVideo();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }

    if (eos) {
        if (!decoder_.SeekToStart()) {
            decoder_.Close();
            OpenNextVideo();
        }
        return;
    }

    if (frame.empty() || width <= 0 || height <= 0) {
        return;
    }

    if (options_.previewMode) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        BlitLayeredFrame(frame, width, height);
    }
}

void LayeredVideoWindow::PaintPreviewFallback(HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    std::wstring text = L"Snoppy Preview";
    if (!statusText_.empty()) {
        text += L"\n" + statusText_;
    }

    DrawTextW(hdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
}

void LayeredVideoWindow::BlitLayeredFrame(const std::vector<std::uint8_t>& bgra, int width, int height) {
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    std::memcpy(bits, bgra.data(), bgra.size());

    HGDIOBJ oldObj = SelectObject(memDc, dib);

    RECT rc{};
    GetWindowRect(hwnd_, &rc);
    SIZE dstSize{ rc.right - rc.left, rc.bottom - rc.top };
    POINT dstPos{ rc.left, rc.top };
    POINT srcPos{ 0, 0 };
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd_, screenDc, &dstPos, &dstSize, memDc, &srcPos, 0, &blend, ULW_ALPHA);

    SelectObject(memDc, oldObj);
    DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}
