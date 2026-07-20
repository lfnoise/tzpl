cmake_minimum_required(VERSION 3.21)
project(tzpl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Options ----------------------------------------------------------------

option(TZPL_BUILD_AUDIO_ENGINE      "Build the engine library and executable"             ON)
option(TZPL_BUILD_SYNTHDEF_COMPILER "Build the synthdef-compiler library and executable"  ON)
option(TZPL_BUILD_LANG              "Build the lang library and executable"                ON)
option(TZPL_BUILD_BRIDGE            "Build the FFI bridge libraries"                      ON)
option(TZPL_BUILD_APP               "Build the audio coding application"                  OFF)
option(TZPL_BUILD_TESTS             "Build integration tests"                             OFF)
option(TZPL_BUILD_BENCHMARKS        "Build benchmarks"                                    OFF)
option(TZPL_BUILD_OSC               "Build OSC (Open Sound Control) support"              OFF)
option(TZPL_BUILD_NATS              "Build NATS messaging support"                       OFF)
option(TZPL_BUILD_GUI               "Build with GUI support (Dear ImGui + GLFW)"         ON)
option(TZPL_BUILD_APP_JUCE          "Build the JUCE GUI application (tzpl_app_juce)"     OFF)

# --- Core dependencies (always needed) --------------------------------------

add_subdirectory(third_party/rtaudio)
if(TZPL_BUILD_OSC)
    add_subdirectory(third_party/oscpack)
endif()
add_subdirectory(shared)

# --- Sub-projects ------------------------------------------------------------

if(TZPL_BUILD_LANG)
    add_subdirectory(lang)
endif()

if(TZPL_BUILD_AUDIO_ENGINE)
    add_subdirectory(engine)
endif()

if(TZPL_BUILD_SYNTHDEF_COMPILER)
    add_subdirectory(synthdef-compiler)
endif()

if(TZPL_BUILD_OSC AND TZPL_BUILD_AUDIO_ENGINE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/osc/CMakeLists.txt)
    add_subdirectory(osc)
endif()

if(TZPL_BUILD_NATS AND TZPL_BUILD_AUDIO_ENGINE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/nats/CMakeLists.txt)
    add_subdirectory(nats)
endif()

if(TZPL_BUILD_BRIDGE AND TZPL_BUILD_LANG AND TZPL_BUILD_AUDIO_ENGINE)
    add_subdirectory(bridge)
endif()
# Note: The synthdef-compiler bridge inside bridge/CMakeLists.txt is guarded
# by if(TARGET synthdef_compiler_lib), so it only builds when
# TZPL_BUILD_SYNTHDEF_COMPILER is ON.
# The OSC bridge inside bridge/CMakeLists.txt is guarded
# by if(TARGET osc_lib), so it only builds when TZPL_BUILD_OSC is ON.
# The NATS bridge inside bridge/CMakeLists.txt is guarded
# by if(TARGET nats_lib), so it only builds when TZPL_BUILD_NATS is ON.

if((TZPL_BUILD_APP OR TZPL_BUILD_APP_JUCE) AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt)
    add_subdirectory(app)
endif()

if(TZPL_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration-tests/CMakeLists.txt)
    add_subdirectory(integration-tests)
endif()

if(TZPL_BUILD_BENCHMARKS AND TZPL_BUILD_SYNTHDEF_COMPILER AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt)
    add_subdirectory(benchmarks)
endif()

# --- Install rules (cmake --install build --prefix dist/tzpl) ----------------
#
# Rules marked COMPONENT dist form the user-facing distribution folder (see
# the `dist` target below); a plain `cmake --install` still installs
# everything, including dev artifacts (libs, headers, integration scripts).

include(GNUInstallDirs)

# Modules: merge lang/modules and bridge/modules into a single modules/ dir
install(DIRECTORY lang/modules/
    DESTINATION modules
    COMPONENT dist
    FILES_MATCHING PATTERN "*.x"
)
if(TZPL_BUILD_BRIDGE)
    install(DIRECTORY bridge/modules/
        DESTINATION modules
        COMPONENT dist
        FILES_MATCHING PATTERN "*.x"
    )
endif()

# Examples
install(DIRECTORY examples/
    DESTINATION examples
    COMPONENT dist
)
install(DIRECTORY integration-tests/scripts/
    DESTINATION examples/integration
    FILES_MATCHING PATTERN "*.x"
)

# Documentation (HTML user-facing docs only)
install(FILES
    lang/docs/Getting_Started.html
    lang/docs/Tzopilotl_by_Example.html
    lang/docs/Builtin_Functions.html
    lang/docs/FFI_Guide.html
    lang/docs/Coroutines.html
    lang/docs/Live_Controls_and_Notebooks.html
    lang/docs/Tzopilotl_Music_Cookbook.html
    DESTINATION docs
    COMPONENT dist
)

# Editor packages
install(DIRECTORY lang/editors/vscode
    DESTINATION editors
    COMPONENT dist
    PATTERN ".DS_Store" EXCLUDE
)
install(DIRECTORY lang/editors/Tzopilotl.tmbundle
    DESTINATION editors
    COMPONENT dist
)
install(DIRECTORY lang/editors/tree-sitter-tzpl
    DESTINATION editors
    COMPONENT dist
    PATTERN "node_modules" EXCLUDE
    PATTERN ".gitignore"   EXCLUDE
)

# Top-level README for the distribution folder
install(FILES packaging/README.txt
    DESTINATION .
    COMPONENT dist
)

# --- Distribution folder + DMG ------------------------------------------------
#
# `cmake --build build --target dist` assembles the drag-anywhere user folder
#
#     Tzopilotl/            <- the unit users install and updates replace
#       Tzopilotl.app         (finds ../modules via the executable walk-up)
#       bin/tzpl
#       modules/  examples/  docs/  editors/  README.txt
#
# from the COMPONENT dist install rules, then wraps it in a compressed DMG.
# User work lives outside the folder (projects), so replacing it is an update.
if(APPLE AND TZPL_BUILD_APP_JUCE)
    set(TZPL_DIST_STAGE "${CMAKE_BINARY_DIR}/dist-stage")
    # Signing and notarization are opt-in via environment variables read by
    # make_dist_dmg.sh at build time (TZPL_CODESIGN_IDENTITY,
    # TZPL_NOTARY_PROFILE); without them the DMG is assembled with the
    # binaries' ad-hoc signatures, so contributor builds need no setup.
    add_custom_target(dist
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${TZPL_DIST_STAGE}"
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                --component dist --prefix "${TZPL_DIST_STAGE}/Tzopilotl"
        COMMAND bash "${CMAKE_SOURCE_DIR}/packaging/make_dist_dmg.sh"
                "${TZPL_DIST_STAGE}"
                "${CMAKE_BINARY_DIR}/Tzopilotl-${PROJECT_VERSION}.dmg"
                "${CMAKE_SOURCE_DIR}/packaging/entitlements.plist"
        DEPENDS tzpl tzpl_app_juce
        COMMENT "Assembling distribution folder and DMG"
        VERBATIM
    )
endif()
