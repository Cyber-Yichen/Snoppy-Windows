#pragma once

#include "LayeredVideoWindow.h"

#include <Windows.h>

#include <filesystem>
#include <memory>
#include <vector>

class ScreenSaverApp {
public:
    explicit ScreenSaverApp(HINSTANCE instance);

    int Run(int argc, wchar_t** argv);

private:
    enum class Mode {
        FullScreen,
        Preview,
        Configure,
    };

    struct ParsedArgs {
        Mode mode = Mode::Configure;
        HWND previewParent = nullptr;
    };

    ParsedArgs ParseArgs(int argc, wchar_t** argv) const;
    int RunConfigure();
    int RunPreview(HWND parent);
    int RunFullScreen();
    int MessageLoop();

    static BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC, LPRECT rect, LPARAM userData);
    static LRESULT CALLBACK InputGuardProc(int code, WPARAM wParam, LPARAM lParam);
    void InstallInputGuard();
    void UninstallInputGuard();

private:
    HINSTANCE instance_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    POINT initialCursor_{};
    bool firstMouseSample_ = true;
    bool exitRequested_ = false;
    std::filesystem::path videoDir_;
    std::vector<std::unique_ptr<LayeredVideoWindow>> windows_;

    inline static ScreenSaverApp* self_ = nullptr;
};
