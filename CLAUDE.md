# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Windows application for controlling monitor brightness: DDC/CI (dxva2) for standard monitors, and USB HID for LG Ultrafine 4K/5K. Uses ImGui with DirectX 11 for the UI, hidapi for USB HID communication, and includes system tray integration and global hotkey support.

## Build Commands

Build requires Windows with MSVC or Clang, CMake 3.20+, and Ninja.

```bash
# Initialize submodules first
git submodule update --init --recursive

# Configure (from Visual Studio Developer Command Prompt or with vcvars64.bat sourced)
cmake --preset x64-debug    # Debug build with MSVC
cmake --preset x64-release  # Release build with MSVC

# Build
cmake --build --preset x64-debug
cmake --build --preset x64-release

# Output binary location
build/x64-debug/LGUltrafineBrightness.exe
build/x64-release/LGUltrafineBrightness.exe
```

## Architecture

The application is organized into these components:

- **app** (`app.cpp/h`) - Main application class orchestrating all components. Creates window, runs message loop, coordinates between brightness control and UI. The window is a `WS_POPUP` tool window anchored to the bottom-right of the taskbar monitor's work area (`positionWindow()`, called every frame: fits height to content and runs the slide/fade-in). The top-edge grab handle (ImGui `InvisibleButton`) reports drags; `positionWindow()` turns the screen-space cursor into a height (bottom stays anchored), saved as `Settings::widgetHeight` at 96 DPI (0 = fit to content). Each tab's content is in a full-width child window (`beginScrollArea`/`endScrollArea`) that scrolls when the window is shorter than the content; the main window has zero horizontal padding so that child's scrollbar isn't clipped. It hides on `WA_INACTIVE`, with a foreground-window check in the run loop as a fallback when activation was refused.

  Frosted glass: `enableGlass()` sets `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW` and extends the frame into the whole client area; the UI then clears to transparent and uses translucent colors (`UIRenderer::setGlass`). ImGui's output is effectively premultiplied alpha, which DWM composites correctly.

- **display** (`display.h`) - `display::Display` interface implemented by each backend. `getBrightness()` must be cheap (cached); it's called every frame.

- **ddc_display** (`ddc_display.cpp/h`) - DDC/CI backend (VCP 0x10). One worker thread per monitor coalesces writes, verifies them by reading back (DDC/CI silently drops commands, especially when another app like Monitorian is polling), and re-reads every 3s while idle to pick up external changes. Names come from `DisplayConfigGetDeviceInfo` (EDID), not dxva2.

- **auto_brightness** (`auto_brightness.cpp/h`) - Lux-to-percent mapping and fade step, applied to every display.

- **schedule** (`schedule.cpp/h`) - Time-of-day brightness curve: points (minute of day, percent) with linear interpolation wrapping at midnight, time zone lookup via `EnumDynamicTimeZoneInformation` + `SystemTimeToTzSpecificLocalTimeEx`, and registry (de)serialization. `schedule_ui.cpp/h` draws the ImGui graph editor (custom `ImDrawList` drawing over an `InvisibleButton`) and the searchable time zone combo. The UI edits `Settings::schedule` in place; the app saves only on committed edits. A manual brightness change sets an override that lasts until the next schedule point.

- **brightness** (`brightness.cpp/h`) - LG Ultrafine 4K/5K backend: HID communication using hidapi. Handles raw brightness values (0x0190-0xd2f0), percent conversion, and stepped adjustments.

- **ui** (`ui.cpp/h`) - DirectX 11 rendering with ImGui. Manages D3D11 device, swap chain, and renders the brightness slider UI.

- **tray** (`tray.cpp/h`) - Windows system tray icon with context menu. Allows minimize-to-tray behavior.

- **hotkey** (`hotkey.cpp/h`) - `hotkey::Binding` (MOD_* + vk, packed into a DWORD for the registry), formatting, and a generic `RegisterHotKey` wrapper. Hotkey ids are Hotkeys-tab entry index + 1: 0 up, 1 down, 2 toggle schedule, 3 show window, 4+ profiles (ids shift when profiles are deleted, so `applyHotkeys()` re-registers everything). Capturing a new combination unregisters all hotkeys and reads WM_KEYDOWN/WM_SYSKEYDOWN in `WndProc`.

- **profiles** (`profiles.cpp/h`) - Saved brightness setups: a default percent plus per-monitor overrides keyed by monitor name, and an optional hotkey. Stored one per line in the `Profiles` registry value; `ProfilesSaved` distinguishes "all deleted" from "never saved".

- **settings** (`settings.cpp/h`) - Persistent settings in `HKCU\Software\LGUltrafineBrightness` and the "Start with Windows" entry in `HKCU\...\CurrentVersion\Run` (launches with `--minimized`).

- **als_sensor** (`als_sensor.cpp/h`) - Ambient light sensor via the Windows Sensor API (COM), used for auto-brightness.

Monitor hot-plug: `WM_DEVICECHANGE`, `WM_DISPLAYCHANGE` and resume-from-sleep schedule a debounced `Application::rescanDisplays()`, which probes/reconnects the LG HID device and re-enumerates DDC/CI monitors. The window height is fit to the UI content each frame (depends on the number of displays).

## Dependencies

External libraries are Git submodules in `external/`:
- **hidapi** - USB HID communication library
- **imgui** - Immediate mode GUI (built with Win32 + DX11 backends)

System libraries: d3d11, dxgi, dwmapi, dxva2, sensorsapi

## Key Constants

LG Ultrafine brightness range: `0x0190` (min) to `0xd2f0` (max), vendor ID `0x043e`.
