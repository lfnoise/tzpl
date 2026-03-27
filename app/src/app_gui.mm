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
//

#include "app_gui.hpp"
#include "tzpl_app_context.hpp"

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
#include <sys/stat.h>

extern volatile sig_atomic_t gShouldQuit;

// ---------------------------------------------------------------------------
// Font discovery
// ---------------------------------------------------------------------------

static bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Search for a monospace font, returning the path or "" for ImGui default.
// Search order:
//   1. DejaVu Sans Mono relative to executable (deployed installs)
//   2. DejaVu Sans Mono in source tree (development builds)
//   3. Monaco (macOS system fallback)
static std::string findMonoFont() {
    // -- Paths relative to executable ------------------------------------
    NSString* exeDir = [[[NSBundle mainBundle] executablePath]
                        stringByDeletingLastPathComponent];
    NSArray* relativePaths = @[
        @"../Resources/fonts/DejaVuSansMono.ttf",       // macOS .app bundle
        @"../share/tzpl/fonts/DejaVuSansMono.ttf",      // FHS-style install
        @"../resources/fonts/DejaVuSansMono.ttf",       // flat install
        @"resources/fonts/DejaVuSansMono.ttf",          // alongside executable
    ];
    for (NSString* rel in relativePaths) {
        NSString* full = [[exeDir stringByAppendingPathComponent:rel]
                          stringByStandardizingPath];
        if ([[NSFileManager defaultManager] fileExistsAtPath:full])
            return std::string([full UTF8String]);
    }

    // -- Compile-time source tree path (development builds) --------------
#ifdef TZPL_FONT_DIR
    {
        std::string path = std::string(TZPL_FONT_DIR) + "/DejaVuSansMono.ttf";
        if (fileExists(path.c_str())) return path;
    }
#endif

    // -- macOS system fallback: Monaco -----------------------------------
    const char* monacoLocations[] = {
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/Supplemental/Monaco.ttf",
    };
    for (auto loc : monacoLocations) {
        if (fileExists(loc)) return loc;
    }

    return ""; // ImGui built-in default
}

static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

// Dampen trackpad scroll speed. The ImGui GLFW backend installs its own
// scroll callback; we replace it, scale the deltas, then forward.
static GLFWscrollfun gPrevScrollCallback = nullptr;
static constexpr float kScrollScale = 0.25f;

static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (gPrevScrollCallback)
        gPrevScrollCallback(window, xoffset * kScrollScale,
                            yoffset * kScrollScale);
}

// Font size switching state (accessible from GLFW key callback)
static int gCurrentFontIdx = 0;
static int gNumFontSizes = 0;
static ImFont** gFonts = nullptr;
static bool gFontChanged = false;

static GLFWkeyfun gPrevKeyCallback = nullptr;

static void keyCallback(GLFWwindow* window, int key, int scancode,
                        int action, int mods) {
    if (action == GLFW_PRESS && (mods & GLFW_MOD_SUPER)) {
        if (key == GLFW_KEY_EQUAL && gCurrentFontIdx < gNumFontSizes - 1) {
            ++gCurrentFontIdx;
            gFontChanged = true;
        }
        if (key == GLFW_KEY_MINUS && gCurrentFontIdx > 0) {
            --gCurrentFontIdx;
            gFontChanged = true;
        }
    }
    // Forward to ImGui's callback
    if (gPrevKeyCallback)
        gPrevKeyCallback(window, key, scancode, action, mods);
}

int runGui(bridge::AppContext& appCtx) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    // No OpenGL context -- we use Metal
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

    // Load font at three sizes for runtime switching (Cmd+= / Cmd+-).
    // Rasterise at native pixel resolution for retina crispness,
    // then scale back with FontGlobalScale.
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);

    const float fontSizes[] = { 14.0f, 16.0f, 18.0f };
    const int numFontSizes = 3;
    ImFont* fonts[numFontSizes] = {};
    int currentFontIdx = 0; // default to 14pt

    std::string fontPath = findMonoFont();
    for (int i = 0; i < numFontSizes; ++i) {
        float atlasSize = fontSizes[i] * xscale;
        if (!fontPath.empty()) {
            fonts[i] = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), atlasSize);
        }
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

    // Replace callbacks: dampen trackpad scroll, handle Cmd+=/- font size
    gPrevScrollCallback = glfwSetScrollCallback(window, scrollCallback);
    gFonts = fonts;
    gNumFontSizes = numFontSizes;
    gCurrentFontIdx = 0;
    gFontChanged = false;
    gPrevKeyCallback = glfwSetKeyCallback(window, keyCallback);

    bool showDemoWindow = true;
    ImVec4 clearColor = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    // --- Main loop ---------------------------------------------------------
    while (!glfwWindowShouldClose(window) && !gShouldQuit) {
        @autoreleasepool {
            glfwPollEvents();

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

            // Apply font size change from GLFW key callback (Cmd+= / Cmd+-)
            if (gFontChanged) {
                currentFontIdx = gCurrentFontIdx;
                io.FontDefault = fonts[currentFontIdx];
                gFontChanged = false;
            }

            // --- Draw UI ---------------------------------------------------
            if (showDemoWindow)
                ImGui::ShowDemoWindow(&showDemoWindow);

            // --- Render ----------------------------------------------------
            ImGui::Render();
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(),
                                           commandBuffer, encoder);

            [encoder endEncoding];
            [commandBuffer presentDrawable:drawable];
            [commandBuffer commit];
        }
    }

    // --- Cleanup -----------------------------------------------------------
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
