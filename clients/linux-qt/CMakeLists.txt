# Linux desktop client: Qt shell over the Rust backend (translator-backend).
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets Network DBus)

qt_standard_project_setup()

# qt_standard_project_setup() does not enable AUTORCC; the flag icons and
# the spec JSON are embedded from .qrc files.
set(CMAKE_AUTORCC ON)

qt_add_executable(translator
    src/main.cpp
    src/AppController.cpp
    src/ActionBar.cpp
    src/AppPickerDialog.cpp
    src/Backend.cpp
    src/SelectionMonitor.cpp
    src/HistoryDialog.cpp
    src/PopupWindow.cpp
    src/SettingsDialog.cpp
    src/Speaker.cpp
    src/Updater.cpp
    src/WordFormatter.cpp
    src/Provider.cpp
    resources/translator.qrc
    ${CMAKE_SOURCE_DIR}/spec/spec.qrc
)

target_include_directories(translator PRIVATE ${CMAKE_SOURCE_DIR}/backend/include)

target_link_libraries(translator PRIVATE
    translator-backend
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
)

target_compile_definitions(translator PRIVATE
    TRANSLATOR_VERSION="${TRANSLATOR_VERSION}"
)

# X11 (xcb): lets the ActionBar detect click-away via _NET_ACTIVE_WINDOW
# changes, since it never takes focus and gets no WindowDeactivate events.
find_library(XCB_LIBRARY xcb)
if(XCB_LIBRARY)
    target_link_libraries(translator PRIVATE ${XCB_LIBRARY})
    target_compile_definitions(translator PRIVATE TRANSLATOR_HAVE_XCB)
endif()

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

# ---- tests ----
option(TRANSLATOR_BUILD_TESTS "Build unit tests" ON)
if(TRANSLATOR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
