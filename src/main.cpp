#include "ScreenSaverApp.h"

#include <Windows.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return 1;
    }

    ScreenSaverApp app(instance);
    const int rc = app.Run(argc, argv);
    LocalFree(argv);
    return rc;
}
