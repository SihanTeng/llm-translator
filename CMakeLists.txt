cmake_minimum_required(VERSION 3.21)

project(translator VERSION 0.2.0 LANGUAGES CXX)

# Version embedded in the app; the release workflow overrides it from the tag
# (-DTRANSLATOR_VERSION=1.2.3) so artifacts never carry a stale version.
set(TRANSLATOR_VERSION "${PROJECT_VERSION}" CACHE STRING "Version embedded in the app")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Rust backend -----------------------------------------------------------
# The shared backend (providers, LLM streaming clients, prompts, history) is
# a Rust crate in backend/, built here via cargo and exposed as the imported
# static library `translator-backend`. Clients (clients/linux-qt today) link
# it through backend/include/translator_backend.h.
find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(BACKEND_TARGET_DIR ${CMAKE_BINARY_DIR}/backend-target)
set(BACKEND_LIB ${BACKEND_TARGET_DIR}/release/libtranslator_backend.a)
file(GLOB_RECURSE BACKEND_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/backend/src/*.rs
    ${CMAKE_SOURCE_DIR}/backend/Cargo.toml
    ${CMAKE_SOURCE_DIR}/backend/Cargo.lock
    ${CMAKE_SOURCE_DIR}/spec/*.json
)
add_custom_command(
    OUTPUT ${BACKEND_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${BACKEND_TARGET_DIR}
            ${CARGO_EXECUTABLE} build --release
            --manifest-path ${CMAKE_SOURCE_DIR}/backend/Cargo.toml
    DEPENDS ${BACKEND_SOURCES}
    COMMENT "cargo build --release (translator-backend)"
    VERBATIM
)
add_custom_target(translator-backend-cargo DEPENDS ${BACKEND_LIB})

add_library(translator-backend STATIC IMPORTED GLOBAL)
set_target_properties(translator-backend PROPERTIES IMPORTED_LOCATION ${BACKEND_LIB})
add_dependencies(translator-backend translator-backend-cargo)

# A Rust staticlib needs the platform threads/dl/m libraries on Linux.
find_package(Threads REQUIRED)

# Must be top-level for ctest to work from the build root; the client adds
# its tests under the TRANSLATOR_BUILD_TESTS option.
enable_testing()

add_subdirectory(clients/linux-qt)

# ---- install + packaging (RPM via CPack) ----

install(TARGETS translator RUNTIME DESTINATION bin)
install(FILES clients/linux-qt/extension/metadata.json
        clients/linux-qt/extension/extension.js
        clients/linux-qt/extension/impl.js
        DESTINATION share/translator/extension)
install(FILES clients/linux-qt/extension/install.sh
        DESTINATION share/translator/extension)
install(FILES packaging/translator.desktop DESTINATION share/applications)
install(FILES packaging/translator.png
        DESTINATION share/icons/hicolor/256x256/apps
        RENAME translator.png)

set(CPACK_PACKAGE_NAME translator)
set(CPACK_PACKAGE_VERSION ${TRANSLATOR_VERSION})
set(CPACK_PACKAGE_CONTACT "translator maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Select-text popup translator (LLM-powered)")
set(CPACK_PACKAGE_DESCRIPTION "Highlight text anywhere to get a Translate bar; click it to stream an LLM translation or a dictionary explanation into a popup. GNOME Wayland (Shell extension) and X11 supported.")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Text")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 6.11")
set(CPACK_RPM_FILE_NAME "translator-${TRANSLATOR_VERSION}-1.fc44.x86_64.rpm")
set(CPACK_GENERATOR "RPM")
include(CPack)
