cmake_minimum_required(VERSION 3.20)
project(LGUltrafineBrightness VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# External Dependencies (submodules)
# =============================================================================

# hidapi (static)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(external/hidapi)

# imgui (no CMakeLists.txt, build manually)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    # Win32 + DirectX11 backends
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC d3d11 dxgi)

# =============================================================================
# Main Application
# =============================================================================

set(SOURCES
    src/main.cpp
    src/brightness.cpp
    src/ddc_display.cpp
    src/auto_brightness.cpp
    src/schedule.cpp
    src/schedule_ui.cpp
    src/profiles.cpp
    src/als_sensor.cpp
    src/ui.cpp
    src/tray.cpp
    src/hotkey.cpp
    src/app.cpp
    src/settings.cpp
)

set(HEADERS
    src/display.h
    src/brightness.h
    src/ddc_display.h
    src/auto_brightness.h
    src/schedule.h
    src/schedule_ui.h
    src/profiles.h
    src/als_sensor.h
    src/ui.h
    src/tray.h
    src/hotkey.h
    src/app.h
    src/settings.h
    src/resource.h
)

# Create executable (WIN32 for no console window)
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} src/resource.rc)

# Force Unicode character set
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    hidapi::hidapi
    imgui
    dwmapi
    sensorsapi
    oleaut32
    advapi32
    dxva2
)

# Set subsystem to Windows and disable auto-manifest (we provide one in resource.rc)
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    LINK_FLAGS "/MANIFEST:NO"
)
