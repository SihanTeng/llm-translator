cmake_minimum_required(VERSION 3.21)

project(translator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets Network DBus)

qt_standard_project_setup()

qt_add_executable(translator
    src/main.cpp
    src/AppController.cpp
    src/ActionBar.cpp
    src/SelectionMonitor.cpp
    src/DeepSeekClient.cpp
    src/PopupWindow.cpp
    src/SettingsDialog.cpp
)

target_link_libraries(translator PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
)

# Optional KDE/wlroots popup placement via wlr-layer-shell (LayerShellQt).
# GNOME does not support layer-shell; the GNOME Shell extension covers it.
# Requires LayerShellQt, e.g. 'sudo dnf install layer-shell-qt' on Fedora.
option(TRANSLATOR_WITH_LAYERSHELL "KDE/wlroots layer-shell popup placement" OFF)
if(TRANSLATOR_WITH_LAYERSHELL)
    find_package(LayerShellQt REQUIRED)
    target_sources(translator PRIVATE
        src/LayerShellPopup.cpp
        src/CursorPosition.cpp
    )
    target_link_libraries(translator PRIVATE LayerShellQt::Interface)
    target_compile_definitions(translator PRIVATE TRANSLATOR_HAVE_LAYERSHELL)
endif()
