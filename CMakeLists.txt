cmake_minimum_required(VERSION 3.20)
project(OpenRaR VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENRAR_BUILD_GUI "Build native Windows GUI (Windows only)" ON)
option(OPENRAR_BUILD_TESTS "Build automated test suite" ON)

find_package(Threads REQUIRED)

function(openrar_configure_target target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /utf-8 /EHsc /bigobj)
        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 1. Core Cross-Platform Library (Windows + Linux + macOS)
# ---------------------------------------------------------------------------
add_library(openrar_core STATIC
    src/core/types.cpp
    src/core/i18n.cpp
    src/core/crypto.cpp
    src/core/deflate.cpp
    src/core/recovery.cpp
    src/core/zip_archive.cpp
    src/core/orar_archive.cpp
    src/core/tar_archive.cpp
    src/core/rar_inspector.cpp
    src/core/extended_archive.cpp
    src/core/archive_manager.cpp
)

target_include_directories(openrar_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(openrar_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(openrar_core PUBLIC bcrypt advapi32)
endif()
openrar_configure_target(openrar_core)

# ---------------------------------------------------------------------------
# 2. Cross-Platform CLI (`openrar`)
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(openrar src/cli/main.cpp src/gui/app.rc)
else()
    add_executable(openrar src/cli/main.cpp)
endif()
target_link_libraries(openrar PRIVATE openrar_core)
openrar_configure_target(openrar)

# ---------------------------------------------------------------------------
# 3. Self-Extracting Archive Stub (`openrar_sfx_stub`)
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(openrar_sfx_stub src/sfx/sfx_stub.cpp src/gui/app.rc)
else()
    add_executable(openrar_sfx_stub src/sfx/sfx_stub.cpp)
endif()
target_link_libraries(openrar_sfx_stub PRIVATE openrar_core)
openrar_configure_target(openrar_sfx_stub)

# ---------------------------------------------------------------------------
# 4. Native Windows Win32 GUI (`OpenRaR-GUI`)
# ---------------------------------------------------------------------------
if(WIN32 AND OPENRAR_BUILD_GUI)
    add_executable(OpenRaR-GUI WIN32
        src/gui/main_win32.cpp
        src/gui/gui_dialogs.cpp
        src/gui/gui_toolbar.cpp
        src/gui/app.rc
        src/gui/app.manifest
    )
    target_link_libraries(OpenRaR-GUI PRIVATE
        openrar_core
        comctl32
        comdlg32
        shell32
        shlwapi
        ole32
        uxtheme
        gdi32
        gdiplus
        user32
    )
    openrar_configure_target(OpenRaR-GUI)
endif()

# ---------------------------------------------------------------------------
# 5. Automated Test Suite (`openrar_tests`)
# ---------------------------------------------------------------------------
if(OPENRAR_BUILD_TESTS)
    enable_testing()
    add_executable(openrar_tests tests/test_openrar.cpp)
    target_link_libraries(openrar_tests PRIVATE openrar_core)
    openrar_configure_target(openrar_tests)
    add_test(NAME OpenRaRTests COMMAND openrar_tests)
endif()

# ---------------------------------------------------------------------------
# 6. Install & CPack Packaging Rules (Windows / Linux / macOS)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS openrar openrar_sfx_stub RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32 AND OPENRAR_BUILD_GUI)
    install(TARGETS OpenRaR-GUI RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES assets/openrar.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)

set(CPACK_PACKAGE_NAME "OpenRaR")
set(CPACK_PACKAGE_VENDOR "OpenRaR Contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast, zero-dependency, open-source C++20 archiver")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OpenRaR Contributors")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
endif()
include(CPack)
