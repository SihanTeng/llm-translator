find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

# Small Qt Test executables, each linked against the production sources they
# exercise. Run via CTest; all use the offscreen platform.
function(add_translator_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE
        Qt6::Test
        Qt6::Widgets
        Qt6::Network
        Qt6::DBus
    )
    target_compile_definitions(${name} PRIVATE TRANSLATOR_VERSION="0.0.0-test")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_translator_test(tst_isnewer
    tst_isnewer.cpp
    ../src/Updater.cpp
)

add_translator_test(tst_sseparsing
    tst_sseparsing.cpp
    ../src/DeepSeekClient.cpp
)

add_translator_test(tst_wordformatter
    tst_wordformatter.cpp
    ../src/WordFormatter.cpp
)

add_translator_test(tst_selectionfilter
    tst_selectionfilter.cpp
    ../src/SelectionMonitor.cpp
)

add_translator_test(tst_appsettings
    tst_appsettings.cpp
    ../src/SettingsDialog.cpp
)

add_translator_test(tst_actionbar
    tst_actionbar.cpp
    ../src/ActionBar.cpp
)
