#include <Windows.h>
#include <cstring>
#include "app.h"

// Custom message for showing window from another instance
constexpr UINT WM_SHOWWINDOW_FROM_INSTANCE = WM_USER + 100;

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nShowCmd)
{
    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"LGUltrafineBrightnessMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running, send message to show its window
        HWND existingWindow = FindWindowW(L"LGUltrafineBrightnessClass", nullptr);
        if (existingWindow) {
            // Send custom message to properly show the window
            PostMessageW(existingWindow, WM_SHOWWINDOW_FROM_INSTANCE, 0, 0);
        }
        return 0;
    }

    // Launched from the "Start with Windows" entry: go straight to the tray
    bool startHidden = lpCmdLine && strstr(lpCmdLine, "--minimized") != nullptr;

    // Create and run application
    app::Application application;

    if (!application.initialize(hInstance, startHidden)) {
        MessageBoxW(nullptr, L"Failed to initialize application", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    int result = application.run();

    application.shutdown();

    if (hMutex) {
        CloseHandle(hMutex);
    }

    return result;
}
