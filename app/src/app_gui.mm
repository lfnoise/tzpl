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
#include "workspace_panel.hpp"
#include "output_panel.hpp"

#include "repl_session.hpp"
#include "nrt_vm.hpp"
#include "module_compiler.hpp"
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
#import <objc/runtime.h>

#include <cstdio>
#include <csignal>
#include <string>
#include <mutex>
#include <filesystem>
#include <sys/stat.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

extern volatile sig_atomic_t gShouldQuit;

// ---------------------------------------------------------------------------
// Global state (declared early so native menu handlers can access them)
// ---------------------------------------------------------------------------

// Font size switching
static int gCurrentFontIdx = 0;
static int gNumFontSizes = 0;
static ImFont** gFonts = nullptr;
static bool gFontChanged = false;

// File operation flags (set by native menu or GLFW key callback)
static bool gFileNew = false;
static bool gFileOpen = false;
static bool gFileSave = false;
static bool gFileSaveAs = false;
static bool gFileSaveCopy = false;
static bool gFileClose = false;

// Edit operation flags (set by native Edit menu)
static bool gEditUndo = false;
static bool gEditRedo = false;
static bool gEditCut = false;
static bool gEditCopy = false;
static bool gEditPaste = false;
static bool gEditSelectAll = false;
static bool gEditClearOutput = false;

// Cursor movement flags (Cmd+Arrow, bypasses ImGui key routing)
static bool gMoveHome = false;
static bool gMoveEnd = false;
static bool gMoveTop = false;
static bool gMoveBottom = false;
static bool gMoveShift = false;  // shift held during move

// Quit request flag (from Cmd+Q or window close button)
static bool gWantsToQuit = false;

// Find operation flags
static bool gFindShow = false;
static bool gFindNext = false;
static bool gFindPrevious = false;
static bool gFindUseSelection = false;
static bool gFindUseSelectionReplace = false;

// Eval request flags
static GuiState* gGuiState = nullptr;

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
// Native macOS menu bar action handler
// ---------------------------------------------------------------------------

@interface TzplMenuHandler : NSObject
- (void)requestQuit:(id)sender;
- (void)fileNew:(id)sender;
- (void)fileOpen:(id)sender;
- (void)fileSave:(id)sender;
- (void)fileSaveAs:(id)sender;
- (void)fileSaveCopy:(id)sender;
- (void)fileClose:(id)sender;
- (void)editUndo:(id)sender;
- (void)editRedo:(id)sender;
- (void)editCut:(id)sender;
- (void)editCopy:(id)sender;
- (void)editPaste:(id)sender;
- (void)editSelectAll:(id)sender;
- (void)editClearOutput:(id)sender;
- (void)findShow:(id)sender;
- (void)findNext:(id)sender;
- (void)findPrevious:(id)sender;
- (void)findUseSelection:(id)sender;
- (void)findUseSelectionReplace:(id)sender;
- (void)fontSizeAction:(id)sender;
@end

@implementation TzplMenuHandler
- (void)requestQuit:(id)sender { gWantsToQuit = true; }
- (void)fileNew:(id)sender     { gFileNew = true; }
- (void)fileOpen:(id)sender    { gFileOpen = true; }
- (void)fileSave:(id)sender    { gFileSave = true; }
- (void)fileSaveAs:(id)sender  { gFileSaveAs = true; }
- (void)fileSaveCopy:(id)sender { gFileSaveCopy = true; }
- (void)fileClose:(id)sender   { gFileClose = true; }
- (void)editUndo:(id)sender    { gEditUndo = true; }
- (void)editRedo:(id)sender    { gEditRedo = true; }
- (void)editCut:(id)sender     { gEditCut = true; }
- (void)editCopy:(id)sender    { gEditCopy = true; }
- (void)editPaste:(id)sender   { gEditPaste = true; }
- (void)editSelectAll:(id)sender { gEditSelectAll = true; }
- (void)editClearOutput:(id)sender { gEditClearOutput = true; }
- (void)findShow:(id)sender    { gFindShow = true; }
- (void)findNext:(id)sender    { gFindNext = true; }
- (void)findPrevious:(id)sender { gFindPrevious = true; }
- (void)findUseSelection:(id)sender { gFindUseSelection = true; }
- (void)findUseSelectionReplace:(id)sender { gFindUseSelectionReplace = true; }
- (void)fontSizeAction:(id)sender {
    NSMenuItem* item = (NSMenuItem*)sender;
    int tag = (int)[item tag];
    if (tag == -1) { // increase
        if (gCurrentFontIdx < gNumFontSizes - 1) {
            ++gCurrentFontIdx;
            gFontChanged = true;
        }
    } else if (tag == -2) { // decrease
        if (gCurrentFontIdx > 0) {
            --gCurrentFontIdx;
            gFontChanged = true;
        }
    } else {
        gCurrentFontIdx = tag;
        gFontChanged = true;
    }
}
@end

static TzplMenuHandler* gMenuHandler = nil;

static void setupNativeMenuBar(const float* fontSizes, int numFontSizes) {
    gMenuHandler = [[TzplMenuHandler alloc] init];

    NSMenu* mainMenu = [[NSMenu alloc] init];

    // -- App menu (required for Quit to work) --------------------------------
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Tzopilotl"];
    [appMenu addItemWithTitle:@"About Tzopilotl"
                       action:nil keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* quitItem = [appMenu addItemWithTitle:@"Quit Tzopilotl"
                       action:@selector(requestQuit:) keyEquivalent:@"q"];
    quitItem.target = gMenuHandler;
    appMenuItem.submenu = appMenu;
    [mainMenu addItem:appMenuItem];

    // -- File menu -----------------------------------------------------------
    NSMenuItem* fileMenuItem = [[NSMenuItem alloc] init];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];

    NSMenuItem* newItem = [fileMenu addItemWithTitle:@"New"
        action:@selector(fileNew:) keyEquivalent:@"n"];
    newItem.target = gMenuHandler;

    NSMenuItem* openItem = [fileMenu addItemWithTitle:@"Open..."
        action:@selector(fileOpen:) keyEquivalent:@"o"];
    openItem.target = gMenuHandler;

    [fileMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* saveItem = [fileMenu addItemWithTitle:@"Save"
        action:@selector(fileSave:) keyEquivalent:@"s"];
    saveItem.target = gMenuHandler;

    NSMenuItem* saveAsItem = [fileMenu addItemWithTitle:@"Save As..."
        action:@selector(fileSaveAs:) keyEquivalent:@"S"];
    saveAsItem.target = gMenuHandler;

    NSMenuItem* saveCopyItem = [fileMenu addItemWithTitle:@"Save a Copy As..."
        action:@selector(fileSaveCopy:) keyEquivalent:@""];
    saveCopyItem.target = gMenuHandler;

    [fileMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* closeItem = [fileMenu addItemWithTitle:@"Close Tab"
        action:@selector(fileClose:) keyEquivalent:@"w"];
    closeItem.target = gMenuHandler;

    fileMenuItem.submenu = fileMenu;
    [mainMenu addItem:fileMenuItem];

    // -- Edit menu -----------------------------------------------------------
    NSMenuItem* editMenuItem = [[NSMenuItem alloc] init];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];

    NSMenuItem* undoItem = [editMenu addItemWithTitle:@"Undo"
        action:@selector(editUndo:) keyEquivalent:@"z"];
    undoItem.target = gMenuHandler;

    NSMenuItem* redoItem = [editMenu addItemWithTitle:@"Redo"
        action:@selector(editRedo:) keyEquivalent:@"Z"];
    redoItem.target = gMenuHandler;

    [editMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* cutItem = [editMenu addItemWithTitle:@"Cut"
        action:@selector(editCut:) keyEquivalent:@"x"];
    cutItem.target = gMenuHandler;

    NSMenuItem* copyItem = [editMenu addItemWithTitle:@"Copy"
        action:@selector(editCopy:) keyEquivalent:@"c"];
    copyItem.target = gMenuHandler;

    NSMenuItem* pasteItem = [editMenu addItemWithTitle:@"Paste"
        action:@selector(editPaste:) keyEquivalent:@"v"];
    pasteItem.target = gMenuHandler;

    [editMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* selectAllItem = [editMenu addItemWithTitle:@"Select All"
        action:@selector(editSelectAll:) keyEquivalent:@"a"];
    selectAllItem.target = gMenuHandler;

    [editMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* clearOutputItem = [editMenu addItemWithTitle:@"Clear Output"
        action:@selector(editClearOutput:) keyEquivalent:@"k"];
    clearOutputItem.target = gMenuHandler;

    editMenuItem.submenu = editMenu;
    [mainMenu addItem:editMenuItem];

    // -- Find menu -----------------------------------------------------------
    NSMenuItem* findMenuItem = [[NSMenuItem alloc] init];
    NSMenu* findMenu = [[NSMenu alloc] initWithTitle:@"Find"];

    NSMenuItem* findItem = [findMenu addItemWithTitle:@"Find..."
        action:@selector(findShow:) keyEquivalent:@"f"];
    findItem.target = gMenuHandler;

    NSMenuItem* findNextItem = [findMenu addItemWithTitle:@"Find Next"
        action:@selector(findNext:) keyEquivalent:@"g"];
    findNextItem.target = gMenuHandler;

    NSMenuItem* findPrevItem = [findMenu addItemWithTitle:@"Find Previous"
        action:@selector(findPrevious:) keyEquivalent:@"G"];
    findPrevItem.target = gMenuHandler;

    [findMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* useSelFind = [findMenu addItemWithTitle:@"Use Selection for Find"
        action:@selector(findUseSelection:) keyEquivalent:@"e"];
    useSelFind.target = gMenuHandler;

    NSMenuItem* useSelReplace = [findMenu addItemWithTitle:@"Use Selection for Replace"
        action:@selector(findUseSelectionReplace:) keyEquivalent:@"E"];
    useSelReplace.target = gMenuHandler;

    findMenuItem.submenu = findMenu;
    [mainMenu addItem:findMenuItem];

    // -- View menu -----------------------------------------------------------
    NSMenuItem* viewMenuItem = [[NSMenuItem alloc] init];
    NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];

    NSMenuItem* fontSizeMenuItem = [[NSMenuItem alloc] init];
    fontSizeMenuItem.title = @"Font Size";
    NSMenu* fontSizeMenu = [[NSMenu alloc] initWithTitle:@"Font Size"];
    for (int i = 0; i < numFontSizes; ++i) {
        NSString* title = [NSString stringWithFormat:@"%.0f pt", fontSizes[i]];
        NSMenuItem* item = [fontSizeMenu addItemWithTitle:title
            action:@selector(fontSizeAction:) keyEquivalent:@""];
        item.target = gMenuHandler;
        item.tag = i;
    }
    fontSizeMenuItem.submenu = fontSizeMenu;
    [viewMenu addItem:fontSizeMenuItem];

    // Font size shortcuts as separate menu items
    NSMenuItem* fontUp = [viewMenu addItemWithTitle:@"Increase Font Size"
        action:@selector(fontSizeAction:) keyEquivalent:@"="];
    fontUp.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    fontUp.target = gMenuHandler;
    fontUp.tag = -1; // handled specially

    NSMenuItem* fontDown = [viewMenu addItemWithTitle:@"Decrease Font Size"
        action:@selector(fontSizeAction:) keyEquivalent:@"-"];
    fontDown.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    fontDown.target = gMenuHandler;
    fontDown.tag = -2; // handled specially

    viewMenuItem.submenu = viewMenu;
    [mainMenu addItem:viewMenuItem];

    [NSApp setMainMenu:mainMenu];
}

// ---------------------------------------------------------------------------
// Native macOS file dialogs
// ---------------------------------------------------------------------------

static std::string nativeOpenFileDialog() {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    UTType* tzplType = [UTType typeWithFilenameExtension:@"x"];
    panel.allowedContentTypes = tzplType ? @[tzplType] : @[];
    panel.allowsOtherFileTypes = YES;
    panel.canChooseDirectories = YES;
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

// Intercept window close to check for unsaved changes
static void windowCloseCallback(GLFWwindow* window) {
    gWantsToQuit = true;
    glfwSetWindowShouldClose(window, GLFW_FALSE);
}

// Native macOS alert for unsaved changes.
// Returns 0 = Save All, 1 = Don't Save, 2 = Cancel.
static int showUnsavedChangesAlert(const std::vector<std::string>& names) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;

    if (names.size() == 1) {
        alert.messageText = [NSString stringWithFormat:
            @"Do you want to save changes to \"%s\"?",
            names[0].c_str()];
        alert.informativeText =
            @"Your changes will be lost if you don't save them.";
    } else {
        alert.messageText = [NSString stringWithFormat:
            @"You have %zu files with unsaved changes.",
            names.size()];
        NSMutableString* info = [NSMutableString
            stringWithString:@"Your changes will be lost if you don't save them.\n"];
        for (auto& n : names)
            [info appendFormat:@"\n  \u2022 %s", n.c_str()];
        alert.informativeText = info;
    }

    [alert addButtonWithTitle:@"Save All"];
    [alert addButtonWithTitle:@"Don't Save"];
    [alert addButtonWithTitle:@"Cancel"];

    // Make "Don't Save" respond to Cmd+D as an accelerator
    alert.buttons[1].keyEquivalent = @"d";
    alert.buttons[1].keyEquivalentModifierMask = NSEventModifierFlagCommand;

    NSModalResponse resp = [alert runModal];
    if (resp == NSAlertFirstButtonReturn)  return 0; // Save All
    if (resp == NSAlertSecondButtonReturn) return 1; // Don't Save
    return 2; // Cancel
}

// Dampen trackpad scroll speed
static GLFWscrollfun gPrevScrollCallback = nullptr;
static constexpr float kScrollScale = 0.25f;

static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (gPrevScrollCallback)
        gPrevScrollCallback(window, xoffset * kScrollScale,
                            yoffset * kScrollScale);
}

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
            if (key == GLFW_KEY_K && !shift) { gEditClearOutput = true; return; }
        }

        // Cmd+Arrow: cursor movement (bypass ImGui nav system)
        if (cmd) {
            if (key == GLFW_KEY_LEFT)  { gMoveHome = true; gMoveShift = shift; return; }
            if (key == GLFW_KEY_RIGHT) { gMoveEnd = true; gMoveShift = shift; return; }
            if (key == GLFW_KEY_UP)    { gMoveTop = true; gMoveShift = shift; return; }
            if (key == GLFW_KEY_DOWN)  { gMoveBottom = true; gMoveShift = shift; return; }
        }

        // Find shortcuts
        if (cmd) {
            if (key == GLFW_KEY_F && !shift) { gFindShow = true; return; }
            if (key == GLFW_KEY_G && !shift) { gFindNext = true; return; }
            if (key == GLFW_KEY_G && shift)  { gFindPrevious = true; return; }
            if (key == GLFW_KEY_E && !shift) { gFindUseSelection = true; return; }
            if (key == GLFW_KEY_E && shift)  { gFindUseSelectionReplace = true; return; }
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

// evaluateCode is now handled by GuiState::asyncEval (see gui_state.cpp)

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
    // Default Latin range plus bullet (U+2022) and arrows (U+2190-21FF)
    static const ImWchar glyphRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x2010, 0x2027, // General Punctuation (includes bullet U+2022)
        0x2190, 0x21FF, // Arrows (includes U+2192 right arrow)
        0,
    };
    for (int i = 0; i < numFontSizes; ++i) {
        float atlasSize = fontSizes[i] * xscale;
        if (!fontPath.empty())
            fonts[i] = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), atlasSize,
                                                      nullptr, glyphRanges);
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

    // Create REPL session for GUI evaluation.
    // Reuse the app's ModuleCompiler so that modules compiled during the
    // initial runSource() keep their cached type objects, avoiding dynamic
    // variable type conflicts when the user re-imports them from the editor.
    ts::REPLSession* session = nullptr;
    std::unique_ptr<ts::REPLSession> ownedSession;
    if (appCtx.nrtvm && appCtx.compiler) {
        if (appCtx.moduleCompiler) {
            ownedSession = std::make_unique<ts::REPLSession>(
                *appCtx.compiler, appCtx.nrtvm->vm, appCtx.target,
                *appCtx.moduleCompiler);
        } else {
            ownedSession = std::make_unique<ts::REPLSession>(
                *appCtx.compiler, appCtx.nrtvm->vm, appCtx.target);
        }
        session = ownedSession.get();
    }

    WorkspacePanel workspacePanel;
    OutputPanel outputPanel;

    guiState.output.append("Tzopilotl. Cmd+Enter: eval block, "
                           "Shift+Enter: eval line, "
                           "Cmd+Shift+Enter: eval file.", LineKind::Info);

    // --- Install GLFW callbacks --------------------------------------------
    glfwSetWindowCloseCallback(window, windowCloseCallback);
    gPrevScrollCallback = glfwSetScrollCallback(window, scrollCallback);
    gFonts = fonts;
    gNumFontSizes = numFontSizes;
    gCurrentFontIdx = 0;
    gFontChanged = false;
    gGuiState = &guiState;
    gPrevKeyCallback = glfwSetKeyCallback(window, keyCallback);

    // Swizzle the GLFW content view's flagsChanged: to suppress Caps Lock
    // events that trigger a noisy macOS TUINSCursorUIController error.
    // This catches all delivery paths including app-activation flag syncs.
    {
        NSView* contentView = [nswin contentView];
        SEL sel = @selector(flagsChanged:);
        Method method = class_getInstanceMethod([contentView class], sel);
        IMP origImpl = method_getImplementation(method);
        IMP newImpl = imp_implementationWithBlock(^(id self, NSEvent* event) {
            if (event.keyCode == 0x39) return; // kVK_CapsLock
            ((void(*)(id, SEL, NSEvent*))origImpl)(self, sel, event);
        });
        method_setImplementation(method, newImpl);
    }

    // --- Native macOS menu bar ---------------------------------------------
    setupNativeMenuBar(fontSizes, numFontSizes);

    ImVec4 clearColor = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    // --- Main loop ---------------------------------------------------------
    while (!glfwWindowShouldClose(window) && !gShouldQuit) {
        @autoreleasepool {
            // Handle quit request (Cmd+Q or window close button)
            if (gWantsToQuit) {
                gWantsToQuit = false;
                auto unsaved = workspacePanel.unsavedFileNames();
                if (unsaved.empty()) break;
                int choice = showUnsavedChangesAlert(unsaved);
                if (choice == 0) { workspacePanel.saveAll(); break; } // Save All
                if (choice == 1) break;                               // Don't Save
                // Cancel -- clear stale flags from modal dialog
                guiState.evalFile = guiState.evalSelection
                                  = guiState.evalLine = false;
                glfwFocusWindow(window);
                [nswin makeFirstResponder:nswin.contentView];
            }

            // Throttle frame rate when idle to save CPU.
            // Use short timeout when animations or async work are active.
            bool needsActivity = guiState.flash.active()
                              || guiState.asyncEval.busy();
            if (needsActivity)
                glfwWaitEventsTimeout(1.0 / 30.0);
            else
                glfwWaitEventsTimeout(0.5);

            // Drain captured print output and collect async eval results
            guiState.printCapture.drain(guiState.output);
            guiState.asyncEval.collect(guiState);

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
            // Process file operations (triggered by native menu or keyboard)
            // ---------------------------------------------------------------
            // Get active editor for this frame
            auto& editorPanel = workspacePanel.activeEditor();

            if (gFileNew) {
                gFileNew = false;
                editorPanel.newTab();
            }
            if (gFileOpen) {
                gFileOpen = false;
                // Native dialog pumps macOS events, so key callbacks (eval
                // shortcuts) can fire during it.  Clear stale eval flags
                // after the dialog returns to avoid evaluating the wrong tab.
                std::string path = nativeOpenFileDialog();
                guiState.evalFile = false;
                guiState.evalSelection = false;
                guiState.evalLine = false;
                if (!path.empty()) {
                    if (std::filesystem::is_directory(path))
                        workspacePanel.addWorkspace(path);
                    else
                        workspacePanel.activeEditor().openFile(path);
                }
                // Restore keyboard focus after native dialog
                glfwFocusWindow(window);
                [nswin makeFirstResponder:nswin.contentView];
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
                guiState.evalFile = guiState.evalSelection = guiState.evalLine = false;
                if (!path.empty())
                    editorPanel.saveAs(path);
                glfwFocusWindow(window);
                [nswin makeFirstResponder:nswin.contentView];
            }
            if (gFileSaveCopy) {
                gFileSaveCopy = false;
                std::string path = nativeSaveFileDialog(
                    editorPanel.activeTabName());
                guiState.evalFile = guiState.evalSelection = guiState.evalLine = false;
                if (!path.empty())
                    editorPanel.saveCopy(path);
                glfwFocusWindow(window);
                [nswin makeFirstResponder:nswin.contentView];
            }
            if (gFileClose) {
                gFileClose = false;
                editorPanel.closeActiveTab();
            }

            // ---------------------------------------------------------------
            // Process edit operations (triggered by native Edit menu)
            // ---------------------------------------------------------------
            if (gEditUndo)      { gEditUndo = false;      editorPanel.undo(); }
            if (gEditRedo)      { gEditRedo = false;      editorPanel.redo(); }
            if (gEditCut)       { gEditCut = false;       editorPanel.cut(); }
            if (gEditCopy)      { gEditCopy = false;      if (!outputPanel.tryCopy()) editorPanel.copy(); }
            if (gEditPaste)     { gEditPaste = false;     editorPanel.paste(); }
            if (gEditSelectAll) { gEditSelectAll = false;  if (!outputPanel.trySelectAll()) editorPanel.selectAll(); }
            if (gEditClearOutput) { gEditClearOutput = false; outputPanel.clear(guiState.output); }

            // ---------------------------------------------------------------
            // Process cursor movement (Cmd+Arrow)
            // ---------------------------------------------------------------
            if (gMoveHome)   { gMoveHome = false;   editorPanel.moveHome(gMoveShift); }
            if (gMoveEnd)    { gMoveEnd = false;    editorPanel.moveEnd(gMoveShift); }
            if (gMoveTop)    { gMoveTop = false;    editorPanel.moveTop(gMoveShift); }
            if (gMoveBottom) { gMoveBottom = false;  editorPanel.moveBottom(gMoveShift); }

            // ---------------------------------------------------------------
            // Process find operations
            // ---------------------------------------------------------------
            if (gFindShow) {
                gFindShow = false;
                auto& fr = editorPanel.findReplace();
                std::string sel = editorPanel.getSelectedText();
                if (!sel.empty())
                    fr.useSelectionForFind(sel);
                fr.show();
                if (auto* ed = editorPanel.activeEditor()) {
                    fr.search(*ed);
                    editorPanel.updateSearchHighlights();
                }
            }
            if (gFindNext) {
                gFindNext = false;
                if (auto* ed = editorPanel.activeEditor()) {
                    editorPanel.findReplace().findNext(*ed);
                    editorPanel.updateSearchHighlights();
                }
            }
            if (gFindPrevious) {
                gFindPrevious = false;
                if (auto* ed = editorPanel.activeEditor()) {
                    editorPanel.findReplace().findPrevious(*ed);
                    editorPanel.updateSearchHighlights();
                }
            }
            if (gFindUseSelection) {
                gFindUseSelection = false;
                std::string sel = editorPanel.getSelectedText();
                if (!sel.empty()) {
                    auto& fr = editorPanel.findReplace();
                    fr.useSelectionForFind(sel);
                    fr.show();
                    if (auto* ed = editorPanel.activeEditor()) {
                        fr.search(*ed);
                        editorPanel.updateSearchHighlights();
                    }
                }
            }
            if (gFindUseSelectionReplace) {
                gFindUseSelectionReplace = false;
                std::string sel = editorPanel.getSelectedText();
                if (!sel.empty()) {
                    editorPanel.findReplace().useSelectionForReplace(sel);
                    editorPanel.findReplace().show();
                }
            }

            // ---------------------------------------------------------------
            // Layout: optional sidebar | editor + output with splitters
            // (Eval requests processed AFTER draw so ImGui tab bar has
            //  updated activeTab_ to match the visually selected tab.)
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

            bool hasSidebar = workspacePanel.hasWorkspaces();
            float vsplitterW = 6.0f;

            // Sidebar (directory outlines)
            if (hasSidebar) {
                float sidebarW = workspacePanel.sidebarWidth();
                workspacePanel.drawSidebar(sidebarW, totalH);
                ImGui::SameLine();

                // Vertical splitter between sidebar and editor
                ImGui::InvisibleButton("##vsplitter",
                                       ImVec2(vsplitterW, totalH));
                if (ImGui::IsItemActive()) {
                    float newW = sidebarW + io.MouseDelta.x;
                    newW = std::max(100.0f, std::min(newW, totalW * 0.5f));
                    workspacePanel.setSidebarWidth(newW);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                ImGui::SameLine();
            }

            // Use actual remaining width after sidebar + spacing
            float rightW = ImGui::GetContentRegionAvail().x;

            float splitterH = 8.0f;
            float usableH = totalH - splitterH;
            float editorH = usableH * guiState.splitRatio;
            float outputH = usableH * (1.0f - guiState.splitRatio);

            // Use a group (not a child window) so editor/output
            // scrollbars are not affected by extra window nesting.
            ImGui::BeginGroup();

            // Editor panel (from active workspace)
            editorPanel.draw(rightW, editorH, guiState);

            // Horizontal splitter
            ImGui::InvisibleButton("##splitter", ImVec2(rightW, splitterH));
            if (ImGui::IsItemActive()) {
                float delta = io.MouseDelta.y / usableH;
                guiState.splitRatio += delta;
                if (guiState.splitRatio < 0.2f) guiState.splitRatio = 0.2f;
                if (guiState.splitRatio > 0.9f) guiState.splitRatio = 0.9f;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            // Output panel
            outputPanel.draw(rightW, outputH, guiState.output);

            ImGui::EndGroup();

            // ---------------------------------------------------------------
            // Process eval requests AFTER draw (activeTab_ is now current)
            // ---------------------------------------------------------------
            if (session && !guiState.asyncEval.busy()) {
                if (guiState.evalSelection) {
                    guiState.evalSelection = false;
                    editorPanel.clearErrorMarkers();
                    std::string code = editorPanel.getSelectedText();
                    int startLine = -1, endLine = -1;
                    if (code.empty()) {
                        code = editorPanel.getCurrentBlockText(startLine, endLine);
                    } else {
                        auto cursor = editorPanel.getCursorPosition();
                        startLine = endLine = cursor.mLine;
                    }
                    guiState.asyncEval.launch(code, appCtx, *session,
                                              startLine, endLine);
                }
                if (guiState.evalLine) {
                    guiState.evalLine = false;
                    editorPanel.clearErrorMarkers();
                    std::string code = editorPanel.getCurrentLineText();
                    auto cursor = editorPanel.getCursorPosition();
                    guiState.asyncEval.launch(code, appCtx, *session,
                                              cursor.mLine, cursor.mLine);
                }
                if (guiState.evalFile) {
                    guiState.evalFile = false;
                    editorPanel.clearErrorMarkers();
                    std::string code = editorPanel.getAllText();
                    guiState.asyncEval.launch(code, appCtx, *session, 0,
                                              editorPanel.getCursorPosition().mLine);
                }
            }

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
