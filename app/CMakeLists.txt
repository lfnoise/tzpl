# app/CMakeLists.txt
# Audio coding application: tzpl + audio engine + synthdef compiler + OSC + GUI
#
# Two GUI app targets can be built from this directory:
#   tzpl_app      -- the Dear ImGui app (TZPL_BUILD_APP, GUI gated on TZPL_BUILD_GUI)
#   tzpl_app_juce -- the JUCE app, sharing main.cpp and the document model
#                    (TZPL_BUILD_APP_JUCE; see juce/ and the section at the bottom)

# ---------------------------------------------------------------------------
# GUI dependencies (Dear ImGui + GLFW) -- only when TZPL_BUILD_GUI is ON
# ---------------------------------------------------------------------------

if(TZPL_BUILD_APP AND TZPL_BUILD_GUI)
    include(FetchContent)

    # --- GLFW ---------------------------------------------------------------
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)

    # --- Dear ImGui (no CMakeLists.txt -- populate and build manually) ------
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.0
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
    endif()

    # --- ImGuiColorTextEdit (vendored, with undo-chunking patch) ------------
    set(IMGUI_TEXTEDIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGuiColorTextEdit)

    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_TEXTEDIT_DIR}/TextEditor.cpp
    )

    if(APPLE)
        list(APPEND IMGUI_SOURCES
            ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
        )
    else()
        list(APPEND IMGUI_SOURCES
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        )
    endif()

    add_library(imgui_lib STATIC ${IMGUI_SOURCES})
    target_include_directories(imgui_lib SYSTEM PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${IMGUI_TEXTEDIT_DIR}
    )
    target_link_libraries(imgui_lib PUBLIC glfw)

    if(APPLE)
        target_link_libraries(imgui_lib PUBLIC
            "-framework Metal"
            "-framework MetalKit"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework QuartzCore"
        )
        set_source_files_properties(
            ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(imgui_lib PUBLIC OpenGL::GL)
    endif()
endif()

# ---------------------------------------------------------------------------
# Application target (Dear ImGui)
# ---------------------------------------------------------------------------

if(TZPL_BUILD_APP)

set(APP_SOURCES src/main.cpp src/project_paths.cpp src/app_config.cpp)

if(TZPL_BUILD_GUI)
    list(APPEND APP_SOURCES
        src/gui_state.cpp # keep in sync with the tzpl_app_juce shared sources below
        src/editor_panel.cpp
        src/workspace_panel.cpp
        src/output_panel.cpp
        src/find_replace.cpp
        src/controls_panel.cpp
        src/widget_draw.cpp
        src/document.cpp
        src/content_hash.cpp
        src/notebook_panel.cpp
        src/plugin_browser_panel.cpp
        src/plugin_tags.cpp
        src/themes.cpp
    )
    if(APPLE)
        list(APPEND APP_SOURCES src/app_gui.mm)
    else()
        list(APPEND APP_SOURCES src/app_gui_gl.cpp)
    endif()
endif()

add_executable(tzpl_app ${APP_SOURCES})

target_link_libraries(tzpl_app
    PRIVATE tzpl_synthdef_compiler_bridge
    PRIVATE tzpl_audio_engine_bridge
    PRIVATE tzpl_clock_bridge
    PRIVATE linenoise
)

if(TZPL_BUILD_GUI)
    target_link_libraries(tzpl_app PRIVATE imgui_lib)
    target_compile_definitions(tzpl_app PRIVATE
        TZPL_HAS_GUI=1
        TZPL_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
    )
    if(APPLE)
        set_source_files_properties(src/app_gui.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )
        target_link_libraries(tzpl_app PRIVATE
            "-framework UniformTypeIdentifiers"
        )
    endif()
endif()

if(TARGET tzpl_osc_bridge)
    target_link_libraries(tzpl_app PRIVATE tzpl_osc_bridge)
    target_compile_definitions(tzpl_app PRIVATE TZPL_HAS_OSC=1)
endif()

if(TARGET tzpl_nats_bridge)
    target_link_libraries(tzpl_app PRIVATE tzpl_nats_bridge)
    target_compile_definitions(tzpl_app PRIVATE TZPL_HAS_NATS=1)
endif()

target_compile_definitions(tzpl_app PRIVATE
    MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
    LANG_MODULES_DIR="${CMAKE_SOURCE_DIR}/lang/modules"
    EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
)

# ---------------------------------------------------------------------------
# Headless document-layer tests (save/load round trips, content hashing,
# interning, persistent history). Runs with an all-null AppContext.
# ---------------------------------------------------------------------------

if(TZPL_BUILD_GUI)
    add_executable(tzpl_doc_tests
        tests/doc_roundtrip_test.cpp
        src/document.cpp
        src/content_hash.cpp
    )
    target_include_directories(tzpl_doc_tests PRIVATE src)
    target_compile_definitions(tzpl_doc_tests PRIVATE
        TZPL_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    )
    target_link_libraries(tzpl_doc_tests
        PRIVATE tzpl_synthdef_compiler_bridge
        PRIVATE tzpl_audio_engine_bridge
        PRIVATE tzpl_clock_bridge
    )

    # tzpl-config reader/writer (startup path + Engine Settings dialog).
    # Pure file I/O -- no engine, no GUI.
    add_executable(tzpl_config_tests
        tests/app_config_test.cpp
        src/app_config.cpp
    )
    target_include_directories(tzpl_config_tests PRIVATE src)

    # Headless widget-interaction tests: an ImGui context with no
    # rendering backend, driven by injected IO events (slider drags,
    # fine control, cmd-click text entry, range gestures).
    add_executable(tzpl_widget_tests
        tests/widget_interaction_test.cpp
        src/widget_draw.cpp
    )
    target_include_directories(tzpl_widget_tests PRIVATE src)
    target_link_libraries(tzpl_widget_tests
        PRIVATE imgui_lib
        PRIVATE tzpl_audio_engine_bridge
    )

    # Headless graph view-model + auto-layout + edit-helper tests
    # (graph editor mode). Links the engine test plugins so the edit
    # submitters can run against a real (audio-stopped) engine.
    add_executable(tzpl_graph_tests
        tests/graph_layout_test.cpp
        src/graph_model.cpp
        src/graph_layout.cpp
        src/graph_edits.cpp
        src/graph_meters.cpp
        ${CMAKE_SOURCE_DIR}/engine/src/tzpl_test_plugins.cpp
    )
    target_include_directories(tzpl_graph_tests PRIVATE src)
    target_link_libraries(tzpl_graph_tests
        PRIVATE tzpl_audio_engine_bridge
    )

    # Host-side spectrum analyzer (Spectrum widgets). Pure math over a
    # vector -- no engine, no GUI.
    add_executable(tzpl_spectrum_tests
        tests/spectrum_test.cpp
    )
    target_link_libraries(tzpl_spectrum_tests
        PRIVATE tzpl_audio_engine_bridge
    )
endif()

# --- install targets ---

include(GNUInstallDirs)

install(TARGETS tzpl_app
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

endif() # TZPL_BUILD_APP

# ---------------------------------------------------------------------------
# JUCE application target (tzpl_app_juce)
#
# Shares src/main.cpp (entry point, engine/VM setup, REPL, NRT paths) and the
# document model with the ImGui app; the GUI layer lives in juce/. runGui()
# is the seam: main.cpp calls it, juce/juce_gui_main.cpp implements it.
# ---------------------------------------------------------------------------

if(TZPL_BUILD_APP_JUCE)
    include(FetchContent)

    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(juce)

    # Document types are declared via PLIST_TO_MERGE rather than
    # DOCUMENT_EXTENSIONS: JUCE lumps all extensions into one entry sharing
    # the app icon, whereas .tzd notebooks get their own document icon
    # (Notebook-Icon.icns, built post-build below).
    juce_add_gui_app(tzpl_app_juce
        PRODUCT_NAME "Tzopilotl"
        COMPANY_NAME "Tzopilotl"
        BUNDLE_ID "com.audiosynth.tzopilotl"
        VERSION "${PROJECT_VERSION}"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Tzopilotl-Icon.png"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT
            "Tzopilotl uses the microphone for audio input to the engine."
        PLIST_TO_MERGE
[[<plist version="1.0">
<dict>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeExtensions</key>
      <array><string>x</string></array>
      <key>LSItemContentTypes</key>
      <array><string>com.audiosynth.tzopilotl.source</string></array>
      <key>CFBundleTypeName</key>
      <string>Tzopilotl Source</string>
      <key>CFBundleTypeRole</key>
      <string>Editor</string>
      <key>CFBundleTypeIconFile</key>
      <string>Icon</string>
      <key>LSHandlerRank</key>
      <string>Default</string>
    </dict>
    <dict>
      <key>CFBundleTypeExtensions</key>
      <array><string>tzd</string></array>
      <key>LSItemContentTypes</key>
      <array><string>com.audiosynth.tzopilotl.notebook</string></array>
      <key>CFBundleTypeName</key>
      <string>Tzopilotl Notebook</string>
      <key>CFBundleTypeRole</key>
      <string>Editor</string>
      <key>CFBundleTypeIconFile</key>
      <string>Notebook-Icon</string>
      <key>LSHandlerRank</key>
      <string>Default</string>
    </dict>
  </array>
  <key>UTExportedTypeDeclarations</key>
  <array>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.audiosynth.tzopilotl.source</string>
      <key>UTTypeDescription</key>
      <string>Tzopilotl Source</string>
      <key>UTTypeConformsTo</key>
      <array><string>public.plain-text</string></array>
      <key>UTTypeIconFile</key>
      <string>Icon</string>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array><string>x</string></array>
      </dict>
    </dict>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.audiosynth.tzopilotl.notebook</string>
      <key>UTTypeDescription</key>
      <string>Tzopilotl Notebook</string>
      <key>UTTypeConformsTo</key>
      <array><string>public.data</string></array>
      <key>UTTypeIconFile</key>
      <string>Notebook-Icon</string>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array><string>tzd</string></array>
      </dict>
    </dict>
  </array>
</dict>
</plist>]]
    )

    # Embedded assets (splash / about-box artwork) -> BinaryData.h
    juce_add_binary_data(tzpl_app_juce_assets
        SOURCES Tzopilotl-Splash.png
    )

    target_sources(tzpl_app_juce PRIVATE
        src/main.cpp
        src/project_paths.cpp
        src/app_config.cpp
        src/gui_state.cpp
        juce/juce_gui_main.cpp
        juce/settings_dialog.cpp
        juce/language_settings_dialog.cpp
        juce/juce_audio_backend.cpp
        juce/main_component.cpp
        juce/tzpl_look_and_feel.cpp
        juce/tzpl_tokeniser.cpp
        juce/editor_pane.cpp
        juce/find_replace_bar.cpp
        juce/output_console.cpp
        juce/perform_view.cpp
        juce/sidebar_panel.cpp
        juce/notebook/cell_component.cpp
        juce/notebook/notebook_view.cpp
        juce/notebook/presets_view.cpp
        juce/notebook/history_window.cpp
        juce/widgets/controls_dispatch.cpp
        juce/widgets/key_dispatch.cpp
        juce/widgets/widget_component.cpp
        juce/widgets/panel_canvas.cpp
        juce/widgets/controls_window.cpp
        juce/plugin_browser_window.cpp
        juce/graph_view.cpp
        juce/status_bar.cpp
        src/plugin_tags.cpp
        src/document.cpp
        src/content_hash.cpp
        src/widget_gestures.cpp
        src/graph_model.cpp
        src/graph_layout.cpp
        src/graph_edits.cpp
        src/graph_meters.cpp
    )
    target_include_directories(tzpl_app_juce PRIVATE src juce)

    target_compile_definitions(tzpl_app_juce PRIVATE
        TZPL_HAS_GUI=1
        TZPL_GUI_JUCE=1
        MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
        LANG_MODULES_DIR="${CMAKE_SOURCE_DIR}/lang/modules"
        EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="Tzopilotl"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    )

    target_link_libraries(tzpl_app_juce
        PRIVATE tzpl_synthdef_compiler_bridge
        PRIVATE tzpl_audio_engine_bridge
        PRIVATE tzpl_clock_bridge
        PRIVATE linenoise
        PRIVATE tzpl_app_juce_assets
        PRIVATE juce::juce_gui_extra
        PRIVATE juce::juce_audio_devices
        PRIVATE juce::juce_audio_utils
        PRIVATE juce::juce_recommended_config_flags
    )

    if(TARGET tzpl_osc_bridge)
        target_link_libraries(tzpl_app_juce PRIVATE tzpl_osc_bridge)
        target_compile_definitions(tzpl_app_juce PRIVATE TZPL_HAS_OSC=1)
    endif()

    if(TARGET tzpl_nats_bridge)
        target_link_libraries(tzpl_app_juce PRIVATE tzpl_nats_bridge)
        target_compile_definitions(tzpl_app_juce PRIVATE TZPL_HAS_NATS=1)
    endif()

    # Replace juceaide's single-representation Icon.icns with the complete
    # standard set (16..1024): macOS shows the placeholder icon in some
    # contexts (e.g. the Cmd+Tab switcher) when only the 1024px ic10
    # representation is present. Runs before signing in the dist pipeline.
    if(APPLE)
        add_custom_command(TARGET tzpl_app_juce POST_BUILD
            COMMAND bash "${CMAKE_SOURCE_DIR}/packaging/make_icns.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/Tzopilotl-Icon.png"
                "$<TARGET_BUNDLE_DIR:tzpl_app_juce>/Contents/Resources/Icon.icns"
            COMMAND bash "${CMAKE_SOURCE_DIR}/packaging/make_icns.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/Tzopilotl-Notebook-Icon.png"
                "$<TARGET_BUNDLE_DIR:tzpl_app_juce>/Contents/Resources/Notebook-Icon.icns"
            COMMENT "Rebuilding Icon.icns + Notebook-Icon.icns with all representations"
        )
    endif()

    # The bundle goes at the root of the distribution folder, next to
    # modules/ (found at runtime via the executable-relative walk-up).
    install(TARGETS tzpl_app_juce
        BUNDLE DESTINATION .
        COMPONENT dist
    )
endif() # TZPL_BUILD_APP_JUCE
