// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  app_gui.mm
//  app
//
//  GUI mode: Dear ImGui with GLFW + Metal backend (macOS).
//  Code editor + REPL output panel.
//

#include "app_gui.hpp"
#include "tzpl_app_context.hpp"
#include "gui_state.hpp"
#include "editor_panel.hpp"
#include "output_panel.hpp"

#include "repl_session.hpp"
#include "nrt_vm.hpp"
#include "diagnostic.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_metal.h"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <cstdio>
#include <csignal>
#include <string>
#include <mutex>
#include <sys/stat.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

extern volatile sig_atomic_t gShouldQuit;

// ---------------------------------------------------------------------------
// Font discovery
// ---------------------------------------------------------------------------

static bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static std::string findMonoFont() {
    NSString* exeDir = [[[NSBundle mainBundle] executablePath]
                        stringByDeletingLastPathComponent];
    NSArray* relativePaths = @[
        @"../Resources/fonts/DejaVuSansMono.ttf",
        @"../share/tzpl/fonts/DejaVuSansMono.ttf",
        @"../resources/fonts/DejaVuSansMono.ttf",
        @"resources/fonts/DejaVuSansMono.ttf",
    ];
    for (NSString* rel in relativePaths) {
        NSString* full = [[exeDir stringByAppendingPathComponent:rel]
                          stringByStandardizingPath];
        if ([[NSFileManager defaultManager] fileExistsAtPath:full])
            return std::string([full UTF8String]);
    }
#ifdef TZPL_FONT_DIR
    {
        std::string path = std::string(TZPL_FONT_DIR) + "/DejaVuSansMono.ttf";
        if (fileExists(path.c_str())) return path;
    }
#endif
    const char* monacoLocations[] = {
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/Supplemental/Monaco.ttf",
    };
    for (auto loc : monacoLocations) {
        if (fileExists(loc)) return loc;
    }
    return "";
}

// ---------------------------------------------------------------------------
// Native macOS file dialogs
// ---------------------------------------------------------------------------

static std::string nativeOpenFileDialog() {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    UTType* tzplType = [UTType typeWithFilenameExtension:@"x"];
    panel.allowedContentTypes = tzplType ? @[tzplType] : @[];
    panel.allowsOtherFileTypes = YES;
    if ([panel runModal] == NSModalResponseOK) {
        return std::string([[panel URL] fileSystemRepresentation]);
    }
    return "";
}

static std::string nativeSaveFileDialog(const std::string& suggestedName) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    UTType* tzplType = [UTType typeWithFilenameExtension:@"x"];
    panel.allowedContentTypes = tzplType ? @[tzplType] : @[];
    panel.allowsOtherFileTypes = YES;
    [panel setNameFieldStringValue:
        [NSString stringWithUTF8String:suggestedName.c_str()]];
    if ([panel runModal] == NSModalResponseOK) {
        return std::string([[panel URL] fileSystemRepresentation]);
    }
    return "";
}

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------

static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

// Dampen trackpad scroll speed
static GLFWscrollfun gPrevScrollCallback = nullptr;
static constexpr float kScrollScale = 0.25f;

static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (gPrevScrollCallback)
        gPrevScrollCallback(window, xoffset * kScrollScale,
                            yoffset * kScrollScale);
}

// Font size switching + eval shortcuts
static int gCurrentFontIdx = 0;
static int gNumFontSizes = 0;
static ImFont** gFonts = nullptr;
static bool gFontChanged = false;
static GuiState* gGuiState = nullptr;

// File operation flags (set by key callback, consumed by main loop)
static bool gFileNew = false;
static bool gFileOpen = false;
static bool gFileSave = false;
static bool gFileSaveAs = false;
static bool gFileClose = false;

static GLFWkeyfun gPrevKeyCallback = nullptr;

static void keyCallback(GLFWwindow* window, int key, int scancode,
                        int action, int mods) {
    if (action == GLFW_PRESS) {
        bool cmd = (mods & GLFW_MOD_SUPER) != 0;
        bool shift = (mods & GLFW_MOD_SHIFT) != 0;

        // Font size: Cmd+= / Cmd+-
        if (cmd && !shift) {
            if (key == GLFW_KEY_EQUAL && gCurrentFontIdx < gNumFontSizes - 1) {
                ++gCurrentFontIdx;
                gFontChanged = true;
            }
            if (key == GLFW_KEY_MINUS && gCurrentFontIdx > 0) {
                --gCurrentFontIdx;
                gFontChanged = true;
            }
        }

        // File shortcuts
        if (cmd) {
            if (key == GLFW_KEY_N && !shift) { gFileNew = true; return; }
            if (key == GLFW_KEY_O && !shift) { gFileOpen = true; return; }
            if (key == GLFW_KEY_S && !shift) { gFileSave = true; return; }
            if (key == GLFW_KEY_S && shift)  { gFileSaveAs = true; return; }
            if (key == GLFW_KEY_W && !shift) { gFileClose = true; return; }
        }

        // Eval shortcuts
        if (gGuiState && key == GLFW_KEY_ENTER) {
            if (cmd && shift) {
                gGuiState->evalFile = true;
                return;
            } else if (cmd) {
                gGuiState->evalSelection = true;
                return;
            } else if (shift) {
                gGuiState->evalLine = true;
                return;
            }
        }
    }

    // Forward to ImGui's callback
    if (gPrevKeyCallback)
        gPrevKeyCallback(window, key, scancode, action, mods);
}

// ---------------------------------------------------------------------------
// Eval helper
// ---------------------------------------------------------------------------

static void evaluateCode(const std::string& code, bridge::AppContext& ctx,
                         GuiState& state, ts::REPLSession& session,
                         int flashStart = -1, int flashEnd = -1) {
    if (code.empty()) return;

    // Acquire NRTVM mutex for thread safety
    std::lock_guard<std::mutex> lock(ctx.nrtvm->mtx);
    ctx.nrtvm->vm.makeCurrent();

    auto result = session.eval(code);

    if (!result.errors.empty()) {
        auto formatted = ts::formatErrorsPlain(result.errors, code, "<editor>");
        for (auto& line : formatted) {
            state.output.append(line, LineKind::Error);
        }
    } else if (result.hasValue) {
        state.output.append("\xe2\x86\x92 " + result.formattedValue
                            + " : " + result.typeName, LineKind::Result);
    }

    ctx.nrtvm->vm.gcHeartbeat();

    // Trigger eval flash
    if (flashStart >= 0 && flashEnd >= 0) {
        state.flash.trigger(flashStart, flashEnd);
    }
}

// ---------------------------------------------------------------------------
// runGui
// ---------------------------------------------------------------------------

int runGui(bridge::AppContext& appCtx) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Tzopilotl", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    // --- Metal setup -------------------------------------------------------
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> commandQueue = [device newCommandQueue];

    NSWindow* nswin = glfwGetCocoaWindow(window);
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.contentsScale = nswin.backingScaleFactor;
    nswin.contentView.layer = layer;
    nswin.contentView.wantsLayer = YES;

    // --- Dear ImGui setup --------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load font at three sizes for runtime switching
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);

    const float fontSizes[] = { 14.0f, 16.0f, 18.0f };
    const int numFontSizes = 3;
    ImFont* fonts[numFontSizes] = {};
    int currentFontIdx = 0;

    std::string fontPath = findMonoFont();
    for (int i = 0; i < numFontSizes; ++i) {
        float atlasSize = fontSizes[i] * xscale;
        if (!fontPath.empty())
            fonts[i] = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), atlasSize);
        if (!fonts[i]) {
            ImFontConfig cfg;
            cfg.SizePixels = atlasSize;
            fonts[i] = io.Fonts->AddFontDefault(&cfg);
        }
    }
    io.FontGlobalScale = 1.0f / xscale;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplMetal_Init(device);

    // --- GUI state and panels ----------------------------------------------
    GuiState guiState;

    // Redirect VM print output to capture pipe
    if (appCtx.nrtvm && guiState.printCapture.captureFile()) {
        appCtx.nrtvm->vm.setPrintOutput(guiState.printCapture.captureFile());
    }

    // Create REPL session for GUI evaluation
    ts::REPLSession* session = nullptr;
    std::unique_ptr<ts::REPLSession> ownedSession;
    if (appCtx.nrtvm && appCtx.compiler) {
        ownedSession = std::make_unique<ts::REPLSession>(
            *appCtx.compiler, appCtx.nrtvm->vm, appCtx.target);
        session = ownedSession.get();
    }

    EditorPanel editorPanel;
    OutputPanel outputPanel;

    guiState.output.append("Tzopilotl. Cmd+Enter: eval block, "
                           "Shift+Enter: eval line, "
                           "Cmd+Shift+Enter: eval file.", LineKind::Info);

    // --- Install GLFW callbacks --------------------------------------------
    gPrevScrollCallback = glfwSetScrollCallback(window, scrollCallback);
    gFonts = fonts;
    gNumFontSizes = numFontSizes;
    gCurrentFontIdx = 0;
    gFontChanged = false;
    gGuiState = &guiState;
    gPrevKeyCallback = glfwSetKeyCallback(window, keyCallback);

    ImVec4 clearColor = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    // --- Main loop ---------------------------------------------------------
    while (!glfwWindowShouldClose(window) && !gShouldQuit) {
        @autoreleasepool {
            glfwPollEvents();

            // Drain captured print output
            guiState.printCapture.drain(guiState.output);

            // Update eval flash
            guiState.flash.update(io.DeltaTime);

            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            layer.drawableSize = CGSizeMake(width, height);

            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) continue;

            MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor new];
            rpd.colorAttachments[0].texture = drawable.texture;
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.colorAttachments[0].clearColor = MTLClearColorMake(
                clearColor.x * clearColor.w,
                clearColor.y * clearColor.w,
                clearColor.z * clearColor.w,
                clearColor.w);

            id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
            id<MTLRenderCommandEncoder> encoder =
                [commandBuffer renderCommandEncoderWithDescriptor:rpd];

            // Start Dear ImGui frame
            ImGui_ImplMetal_NewFrame(rpd);
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Apply font size change
            if (gFontChanged) {
                currentFontIdx = gCurrentFontIdx;
                io.FontDefault = fonts[currentFontIdx];
                gFontChanged = false;
            }

            // ---------------------------------------------------------------
            // File menu
            // ---------------------------------------------------------------
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New", "Cmd+N"))      gFileNew = true;
                    if (ImGui::MenuItem("Open...", "Cmd+O"))   gFileOpen = true;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Save", "Cmd+S"))      gFileSave = true;
                    if (ImGui::MenuItem("Save As...", "Cmd+Shift+S"))
                                                               gFileSaveAs = true;
                    if (ImGui::MenuItem("Save a Copy As..."))  {
                        std::string path = nativeSaveFileDialog(
                            editorPanel.activeTabName());
                        if (!path.empty())
                            editorPanel.saveCopy(path);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Close Tab", "Cmd+W")) gFileClose = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    if (ImGui::BeginMenu("Font Size")) {
                        for (int i = 0; i < numFontSizes; ++i) {
                            char label[32];
                            snprintf(label, sizeof(label), "%.0f pt", fontSizes[i]);
                            if (ImGui::MenuItem(label, nullptr,
                                                currentFontIdx == i)) {
                                gCurrentFontIdx = i;
                                gFontChanged = true;
                            }
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            // ---------------------------------------------------------------
            // Process file operations
            // ---------------------------------------------------------------
            if (gFileNew) {
                gFileNew = false;
                editorPanel.newTab();
            }
            if (gFileOpen) {
                gFileOpen = false;
                std::string path = nativeOpenFileDialog();
                if (!path.empty())
                    editorPanel.openFile(path);
            }
            if (gFileSave) {
                gFileSave = false;
                if (editorPanel.hasFilePath()) {
                    editorPanel.save();
                } else {
                    // No path yet -- fall through to Save As
                    gFileSaveAs = true;
                }
            }
            if (gFileSaveAs) {
                gFileSaveAs = false;
                std::string path = nativeSaveFileDialog(
                    editorPanel.activeTabName());
                if (!path.empty())
                    editorPanel.saveAs(path);
            }
            if (gFileClose) {
                gFileClose = false;
                editorPanel.closeActiveTab();
            }

            // ---------------------------------------------------------------
            // Process eval requests from keyboard shortcuts
            // ---------------------------------------------------------------
            if (session) {
                if (guiState.evalSelection) {
                    guiState.evalSelection = false;
                    editorPanel.clearErrorMarkers();
                    std::string code = editorPanel.getSelectedText();
                    int startLine = -1, endLine = -1;
                    if (code.empty()) {
                        // No selection: eval current block
                        code = editorPanel.getCurrentBlockText(startLine, endLine);
                    } else {
                        auto cursor = editorPanel.getCursorPosition();
                        startLine = endLine = cursor.mLine;
                    }
                    evaluateCode(code, appCtx, guiState, *session,
                                 startLine, endLine);
                }
                if (guiState.evalLine) {
                    guiState.evalLine = false;
                    editorPanel.clearErrorMarkers();
                    std::string code = editorPanel.getCurrentLineText();
                    auto cursor = editorPanel.getCursorPosition();
                    evaluateCode(code, appCtx, guiState, *session,
                                 cursor.mLine, cursor.mLine);
                }
                if (guiState.evalFile) {
                    guiState.evalFile = false;
                    editorPanel.clearErrorMarkers();
                    std::string code = editorPanel.getAllText();
                    evaluateCode(code, appCtx, guiState, *session, 0,
                                 editorPanel.getCursorPosition().mLine);
                }
            }

            // ---------------------------------------------------------------
            // Layout: editor on top, output on bottom, with splitter
            // ---------------------------------------------------------------
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::Begin("##MainWindow", nullptr,
                         ImGuiWindowFlags_NoDecoration
                         | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoScrollWithMouse
                         | ImGuiWindowFlags_NoBringToFrontOnFocus);

            float totalW = ImGui::GetContentRegionAvail().x;
            float totalH = ImGui::GetContentRegionAvail().y;
            float splitterH = 8.0f;
            float usableH = totalH - splitterH;
            float editorH = usableH * guiState.splitRatio;
            float outputH = usableH * (1.0f - guiState.splitRatio);

            // Editor panel
            editorPanel.draw(totalW, editorH, guiState);

            // Horizontal splitter
            ImGui::InvisibleButton("##splitter", ImVec2(totalW, splitterH));
            if (ImGui::IsItemActive()) {
                float delta = io.MouseDelta.y / usableH;
                guiState.splitRatio += delta;
                if (guiState.splitRatio < 0.2f) guiState.splitRatio = 0.2f;
                if (guiState.splitRatio > 0.9f) guiState.splitRatio = 0.9f;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            // Output panel
            outputPanel.draw(totalW, outputH, guiState.output,
                             session, appCtx.nrtvm);

            ImGui::End();

            // Render
            ImGui::Render();
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(),
                                           commandBuffer, encoder);

            [encoder endEncoding];
            [commandBuffer presentDrawable:drawable];
            [commandBuffer commit];
        }
    }

    // --- Cleanup -----------------------------------------------------------
    gGuiState = nullptr;
    ownedSession.reset();

    // Restore VM print output to stdout
    if (appCtx.nrtvm)
        appCtx.nrtvm->vm.setPrintOutput(stdout);

    ImGui_ImplMetal_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
