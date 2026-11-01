cmake_minimum_required(VERSION 3.28)
project(superfar VERSION 0.6.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets DBus Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0>=2.74)
# Sandboxed image decoding for the preview (see CLAUDE.md, Dependencies).
pkg_check_modules(GLYCIN REQUIRED IMPORTED_TARGET glycin-2)

qt_standard_project_setup()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
# GLib headers use "signals" as an identifier, so Qt keywords are off.
add_compile_definitions(QT_NO_KEYWORDS)

add_subdirectory(src/core)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
