find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

# Small Qt Test executables. App-shell sources (../src) are compiled in
# directly; platform-neutral code comes from the translator-core library.
# Tests touching providers()/Prompts() must also embed ../spec/spec.qrc
# (the spec JSON lives in the binary's resources). All use the offscreen
# platform.
function(add_translator_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE
        translator-core
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
)

add_translator_test(tst_wordformatter
    tst_wordformatter.cpp
)

add_translator_test(tst_selectionfilter
    tst_selectionfilter.cpp
    ../src/SelectionMonitor.cpp
)

add_translator_test(tst_appsettings
    tst_appsettings.cpp
    ../src/SettingsDialog.cpp
    ../src/AppPickerDialog.cpp
    ../spec/spec.qrc
)

add_translator_test(tst_apppicker
    tst_apppicker.cpp
    ../src/AppPickerDialog.cpp
)

add_translator_test(tst_historystore
    tst_historystore.cpp
)

add_translator_test(tst_languages
    tst_languages.cpp
    ../resources/translator.qrc
)

add_translator_test(tst_providerspec
    tst_providerspec.cpp
    ../spec/spec.qrc
)

add_translator_test(tst_actionbar
    tst_actionbar.cpp
    ../src/ActionBar.cpp
)
