#include "ScreenSaverApp.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <string>

namespace {
std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return s;
}

std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}
}

ScreenSaverApp::ScreenSaverApp(HINSTANCE instance)
    : instance_(instance),
      videoDir_(GetExecutableDirectory() / L"video") {
    self_ = this;
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
}

int ScreenSaverApp::Run(int argc, wchar_t** argv) {
    const auto parsed = ParseArgs(argc, argv);
    switch (parsed.mode) {
    case Mode::FullScreen:
        return RunFullScreen();
    case Mode::Preview:
        return RunPreview(parsed.previewParent);
    case Mode::Configure:
    default:
        return RunConfigure();
    }
}

ScreenSaverApp::ParsedArgs ScreenSaverApp::ParseArgs(int argc, wchar_t** argv) const {
    ParsedArgs result{};
    if (argc <= 1) {
        result.mode = Mode::Configure;
        return result;
    }

    auto arg1 = ToLower(argv[1]);
    if (arg1.starts_with(L"/s") || arg1.starts_with(L"-s")) {
        result.mode = Mode::FullScreen;
        return result;
    }

    if (arg1.starts_with(L"/p") || arg1.starts_with(L"-p")) {
        result.mode = Mode::Preview;
        if (argc >= 3) {
            result.previewParent = reinterpret_cast<HWND>(_wcstoui64(argv[2], nullptr, 10));
        } else {
            const auto pos = arg1.find_first_of(L": ");
            if (pos != std::wstring::npos) {
                result.previewParent = reinterpret_cast<HWND>(_wcstoui64(arg1.substr(pos + 1).c_str(), nullptr, 10));
            }
        }
        return result;
    }

    result.mode = Mode::Configure;
    return result;
}

int ScreenSaverApp::RunConfigure() {
    std::wstring message =
        L"Snoppy Windows Screen Saver\n\n"
        L"Usage:\n"
        L"  /s  Full screen saver\n"
        L"  /p <HWND>  Preview\n"
        L"  /c  Configure\n\n"
        L"Put your source videos into the video folder next to Snoppy.scr.\n\n"
        L"Current video path:\n" + videoDir_.wstring();

    MessageBoxW(nullptr, message.c_str(), L"Snoppy", MB_OK | MB_ICONINFORMATION);
    return 0;
}

int ScreenSaverApp::RunPreview(HWND parent) {
    if (!parent || !IsWindow(parent)) {
        return RunConfigure();
    }

    RECT rc{};
    GetClientRect(parent, &rc);

    LayeredVideoWindow::Options options{};
    options.parent = parent;
    options.previewMode = true;
    options.bounds = rc;
    options.videoDirectory = videoDir_;

    auto window = std::make_unique<LayeredVideoWindow>(options);
    if (!window->Create(instance_)) {
        return 1;
    }
    window->Show();
    windows_.push_back(std::move(window));

    return MessageLoop();
}

BOOL CALLBACK ScreenSaverApp::MonitorEnumProc(HMONITOR, HDC, LPRECT rect, LPARAM userData) {
    auto* app = reinterpret_cast<ScreenSaverApp*>(userData);

    LayeredVideoWindow::Options options{};
    options.previewMode = false;
    options.parent = nullptr;
    options.bounds = *rect;
    options.videoDirectory = app->videoDir_;

    auto window = std::make_unique<LayeredVideoWindow>(options);
    if (window->Create(app->instance_)) {
        window->Show();
        app->windows_.push_back(std::move(window));
    }

    return TRUE;
}

void ScreenSaverApp::InstallInputGuard() {
    GetCursorPos(&initialCursor_);
    firstMouseSample_ = true;
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, &InputGuardProc, nullptr, 0);
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &InputGuardProc, nullptr, 0);
}

void ScreenSaverApp::UninstallInputGuard() {
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
}

LRESULT CALLBACK ScreenSaverApp::InputGuardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && self_) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN || wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN) {
            self_->exitRequested_ = true;
            PostQuitMessage(0);
        }

        if (wParam == WM_MOUSEMOVE) {
            auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            if (self_->firstMouseSample_) {
                self_->initialCursor_ = info->pt;
                self_->firstMouseSample_ = false;
            } else {
                const int dx = std::abs(info->pt.x - self_->initialCursor_.x);
                const int dy = std::abs(info->pt.y - self_->initialCursor_.y);
                if (dx > 3 || dy > 3) {
                    self_->exitRequested_ = true;
                    PostQuitMessage(0);
                }
            }
        }
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

int ScreenSaverApp::RunFullScreen() {
    EnumDisplayMonitors(nullptr, nullptr, &MonitorEnumProc, reinterpret_cast<LPARAM>(this));
    if (windows_.empty()) {
        return 1;
    }

    InstallInputGuard();
    const int rc = MessageLoop();
    UninstallInputGuard();
    return rc;
}

int ScreenSaverApp::MessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
