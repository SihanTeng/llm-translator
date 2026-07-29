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

# ---- install + packaging (RPM via CPack) ----

install(TARGETS translator RUNTIME DESTINATION bin)
install(FILES extension/metadata.json extension/extension.js extension/impl.js
        DESTINATION share/translator/extension)
install(FILES extension/install.sh
        DESTINATION share/translator/extension)
install(FILES packaging/translator.desktop DESTINATION share/applications)
install(FILES packaging/translator.png
        DESTINATION share/icons/hicolor/256x256/apps
        RENAME translator.png)

set(CPACK_PACKAGE_NAME translator)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "translator maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Select-text popup translator (LLM-powered)")
set(CPACK_PACKAGE_DESCRIPTION "Highlight text anywhere to get a Translate bar; click it to stream an LLM translation or a dictionary explanation into a popup. GNOME Wayland (Shell extension) and X11 supported.")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Text")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 6.11")
set(CPACK_RPM_FILE_NAME "translator-${PROJECT_VERSION}-1.fc44.x86_64.rpm")
set(CPACK_GENERATOR "RPM")
include(CPack)
