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
#include <cstring>
#include "tzpl_app_context.hpp"
#include "tzpl_ui_state.hpp"
#include "gui_state.hpp"
#include "editor_panel.hpp"
#include "workspace_panel.hpp"
#include "output_panel.hpp"
#include "controls_panel.hpp"
#include "notebook_panel.hpp"
#include "plugin_browser_panel.hpp"
#include "widget_draw.hpp"
#include "themes.hpp"

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
#include <cctype>
#include <string>
#include <mutex>
#include <filesystem>
#include <sys/stat.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

extern volatile sig_atomic_t gShouldQuit;

// ---------------------------------------------------------------------------
// Application command queue
//
// Native menu handlers and the GLFW key callback push commands; the frame
// loop drains the queue once per frame and dispatches them in order.
// ---------------------------------------------------------------------------

enum class AppCmd {
    Quit,
    // File
    FileNew, FileNewNotebook, FileOpen, FileOpenFolder, FileSave, FileSaveAs,
    FileSaveCopy, FileClose,
    // Edit
    EditUndo, EditRedo, EditCut, EditCopy, EditPaste, EditSelectAll,
    EditClearOutput, EditToggleComment, EditIndent, EditOutdent,
    // Cursor movement (Cmd+Arrow; `shift` extends selection)
    MoveHome, MoveEnd, MoveTop, MoveBottom,
    // Find
    FindShow, FindNext, FindPrevious, FindUseSelection, FindUseSelectionReplace,
    // View (`arg` = font index for FontSet, theme index for ThemeSet)
    FontIncrease, FontDecrease, FontSet, ThemeSet,
    // Swap the center pane between the open notebook and the editor tabs
    // (the notebook document stays alive either way)
    ToggleNotebookView,
    TogglePluginBrowser,
    // Eval (dispatched by setting GuiState flags; processed after draw)
    EvalSelection, EvalLine, EvalFile,
};

struct AppCommand {
    AppCmd cmd;
    bool shift = false;
    int arg = 0;
};

struct AppCmdQueue {
    void push(AppCmd cmd, bool shift = false, int arg = 0) {
        std::lock_guard<std::mutex> lock(mtx_);
        cmds_.push_back({cmd, shift, arg});
    }
    std::vector<AppCommand> drain() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<AppCommand> out;
        out.swap(cmds_);
        return out;
    }
    // Discard pending eval commands. Native modal dialogs pump macOS events,
    // so eval shortcuts can be enqueued while a dialog is up; call this after
    // a modal returns to avoid evaluating against the wrong tab.
    void purgeEvals() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::erase_if(cmds_, [](AppCommand const& c) {
            return c.cmd == AppCmd::EvalSelection || c.cmd == AppCmd::EvalLine
                || c.cmd == AppCmd::EvalFile;
        });
    }
private:
    std::mutex mtx_;
    std::vector<AppCommand> cmds_;
};

static AppCmdQueue gCmdQueue;

// Deferred key injection for text-field clipboard operations.
// When the native Edit menu fires while an ImGui text input is focused,
// we inject the shortcut key into ImGui's IO queue so the text field
// handles cut/copy/paste/undo/redo/select-all instead of the editor.
static ImGuiKey gDeferredEditKey = ImGuiKey_None;
static bool gDeferredEditShift = false;
static int gDeferredEditFrame = 0;  // 0=idle, 1=inject key-down, 2=inject key-up

// io.WantTextInput as of the END of the last frame. Commands dispatch
// right after NewFrame, which has already reset WantTextInput; the cell
// TextEditors only re-assert it later, during their draw. Routing edit
// commands on the live flag alone would steer a focused cell's paste
// into the (hidden) editor panel, so route on either.
static bool gTextFieldFocusedLastFrame = false;

// Quit request flag (from Cmd+Q or window close button); handled at the top
// of the frame loop, outside command dispatch.
static bool gWantsToQuit = false;

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
- (void)fileNewNotebook:(id)sender;
- (void)fileOpen:(id)sender;
- (void)fileOpenFolder:(id)sender;
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
- (void)editToggleComment:(id)sender;
- (void)editIndent:(id)sender;
- (void)editOutdent:(id)sender;
- (void)findShow:(id)sender;
- (void)viewToggleNotebook:(id)sender;
- (void)viewTogglePluginBrowser:(id)sender;
- (void)findNext:(id)sender;
- (void)findPrevious:(id)sender;
- (void)findUseSelection:(id)sender;
- (void)findUseSelectionReplace:(id)sender;
- (void)fontSizeAction:(id)sender;
- (void)themeAction:(id)sender;
@end

@implementation TzplMenuHandler
- (void)requestQuit:(id)sender { gWantsToQuit = true; }
- (void)fileNew:(id)sender     { gCmdQueue.push(AppCmd::FileNew); }
- (void)fileNewNotebook:(id)sender { gCmdQueue.push(AppCmd::FileNewNotebook); }
- (void)fileOpen:(id)sender    { gCmdQueue.push(AppCmd::FileOpen); }
- (void)fileOpenFolder:(id)sender { gCmdQueue.push(AppCmd::FileOpenFolder); }
- (void)fileSave:(id)sender    { gCmdQueue.push(AppCmd::FileSave); }
- (void)fileSaveAs:(id)sender  { gCmdQueue.push(AppCmd::FileSaveAs); }
- (void)fileSaveCopy:(id)sender { gCmdQueue.push(AppCmd::FileSaveCopy); }
- (void)fileClose:(id)sender   { gCmdQueue.push(AppCmd::FileClose); }
- (void)editUndo:(id)sender    { gCmdQueue.push(AppCmd::EditUndo); }
- (void)editRedo:(id)sender    { gCmdQueue.push(AppCmd::EditRedo); }
- (void)editCut:(id)sender     { gCmdQueue.push(AppCmd::EditCut); }
- (void)editCopy:(id)sender    { gCmdQueue.push(AppCmd::EditCopy); }
- (void)editPaste:(id)sender   { gCmdQueue.push(AppCmd::EditPaste); }
- (void)editSelectAll:(id)sender { gCmdQueue.push(AppCmd::EditSelectAll); }
- (void)editClearOutput:(id)sender { gCmdQueue.push(AppCmd::EditClearOutput); }
- (void)editToggleComment:(id)sender { gCmdQueue.push(AppCmd::EditToggleComment); }
- (void)editIndent:(id)sender { gCmdQueue.push(AppCmd::EditIndent); }
- (void)editOutdent:(id)sender { gCmdQueue.push(AppCmd::EditOutdent); }
- (void)findShow:(id)sender    { gCmdQueue.push(AppCmd::FindShow); }
- (void)findNext:(id)sender    { gCmdQueue.push(AppCmd::FindNext); }
- (void)findPrevious:(id)sender { gCmdQueue.push(AppCmd::FindPrevious); }
- (void)findUseSelection:(id)sender { gCmdQueue.push(AppCmd::FindUseSelection); }
- (void)findUseSelectionReplace:(id)sender { gCmdQueue.push(AppCmd::FindUseSelectionReplace); }
- (void)viewToggleNotebook:(id)sender { gCmdQueue.push(AppCmd::ToggleNotebookView); }
- (void)viewTogglePluginBrowser:(id)sender { gCmdQueue.push(AppCmd::TogglePluginBrowser); }
- (void)fontSizeAction:(id)sender {
    NSMenuItem* item = (NSMenuItem*)sender;
    int tag = (int)[item tag];
    if (tag == -1)      gCmdQueue.push(AppCmd::FontIncrease);
    else if (tag == -2) gCmdQueue.push(AppCmd::FontDecrease);
    else                gCmdQueue.push(AppCmd::FontSet, false, tag);
}
- (void)themeAction:(id)sender {
    NSMenuItem* item = (NSMenuItem*)sender;
    gCmdQueue.push(AppCmd::ThemeSet, false, (int)[item tag]);
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

    NSMenuItem* newNotebookItem = [fileMenu addItemWithTitle:@"New Notebook"
        action:@selector(fileNewNotebook:) keyEquivalent:@"N"];
    newNotebookItem.target = gMenuHandler;

    NSMenuItem* openItem = [fileMenu addItemWithTitle:@"Open..."
        action:@selector(fileOpen:) keyEquivalent:@"o"];
    openItem.target = gMenuHandler;

    NSMenuItem* openFolderItem = [fileMenu addItemWithTitle:@"Open Folder..."
        action:@selector(fileOpenFolder:) keyEquivalent:@"O"];
    openFolderItem.target = gMenuHandler;

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

    [editMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* toggleCommentItem = [editMenu addItemWithTitle:@"Toggle Line Comment"
        action:@selector(editToggleComment:) keyEquivalent:@"/"];
    toggleCommentItem.target = gMenuHandler;

    NSMenuItem* indentItem = [editMenu addItemWithTitle:@"Indent"
        action:@selector(editIndent:) keyEquivalent:@"]"];
    indentItem.target = gMenuHandler;

    NSMenuItem* outdentItem = [editMenu addItemWithTitle:@"Outdent"
        action:@selector(editOutdent:) keyEquivalent:@"["];
    outdentItem.target = gMenuHandler;

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

    NSMenuItem* toggleNotebookItem = [viewMenu addItemWithTitle:@"Toggle Notebook / Editor"
        action:@selector(viewToggleNotebook:) keyEquivalent:@"\\"];
    toggleNotebookItem.target = gMenuHandler;

    NSMenuItem* pluginBrowserItem = [viewMenu addItemWithTitle:@"Plugin Browser"
        action:@selector(viewTogglePluginBrowser:) keyEquivalent:@"P"];
    pluginBrowserItem.target = gMenuHandler;

    [viewMenu addItem:[NSMenuItem separatorItem]];

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

    NSMenuItem* themeMenuItem = [[NSMenuItem alloc] init];
    themeMenuItem.title = @"Theme";
    NSMenu* themeMenu = [[NSMenu alloc] initWithTitle:@"Theme"];
    for (int i = 0; i < themeCount; ++i) {
        NSMenuItem* item = [themeMenu
            addItemWithTitle:[NSString stringWithUTF8String:kAppThemeNames[i]]
                      action:@selector(themeAction:)
               keyEquivalent:@""];
        item.target = gMenuHandler;
        item.tag = i;
    }
    themeMenuItem.submenu = themeMenu;
    [viewMenu addItem:themeMenuItem];

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
    UTType* notebookType = [UTType typeWithFilenameExtension:@"tzd"];
    NSMutableArray* types = [NSMutableArray array];
    if (tzplType) [types addObject:tzplType];
    if (notebookType) [types addObject:notebookType];
    // A folder must be in the allowed types too, or Open on a selected folder
    // only navigates into it instead of returning it as a workspace root.
    [types addObject:UTTypeFolder];
    panel.allowedContentTypes = types;
    panel.allowsOtherFileTypes = YES;
    panel.canChooseDirectories = YES;
    if ([panel runModal] == NSModalResponseOK) {
        return std::string([[panel URL] fileSystemRepresentation]);
    }
    return "";
}

// Folders only: File > Open Folder... opens a directory as a sidebar
// workspace. Files stay unselectable so a single click + Open picks the
// folder itself (double-click still navigates, as everywhere in macOS).
static std::string nativeOpenFolderDialog() {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.prompt = @"Open Folder";
    panel.message = @"Choose a folder to open as a workspace.";
    if ([panel runModal] == NSModalResponseOK) {
        return std::string([[panel URL] fileSystemRepresentation]);
    }
    return "";
}

static std::string nativeSaveFileDialog(const std::string& suggestedName,
                                        const char* extension = "x") {
    NSSavePanel* panel = [NSSavePanel savePanel];
    UTType* tzplType = [UTType typeWithFilenameExtension:
        [NSString stringWithUTF8String:extension]];
    panel.allowedContentTypes = tzplType ? @[tzplType] : @[];
    panel.allowsOtherFileTypes = YES;
    [panel setNameFieldStringValue:
        [NSString stringWithUTF8String:suggestedName.c_str()]];
    if ([panel runModal] == NSModalResponseOK) {
        return std::string([[panel URL] fileSystemRepresentation]);
    }
    return "";
}

// Case-insensitive: macOS filesystems are case-insensitive, so Song.TZD is
// the same notebook document as song.tzd.
static bool hasExtension(std::string const& path, const char* ext) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = path.substr(dot + 1);
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    return e == ext;
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

// Native macOS alert for closing a single modified tab.
// Returns 0 = Save, 1 = Don't Save (discard), 2 = Cancel.
static int showCloseTabAlert(const std::string& name) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = [NSString stringWithFormat:
        @"Do you want to save changes to \"%s\"?", name.c_str()];
    alert.informativeText =
        @"Your changes will be lost if you don't save them.";

    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Don't Save"];
    [alert addButtonWithTitle:@"Cancel"];

    alert.buttons[1].keyEquivalent = @"d";
    alert.buttons[1].keyEquivalentModifierMask = NSEventModifierFlagCommand;

    NSModalResponse resp = [alert runModal];
    if (resp == NSAlertFirstButtonReturn)  return 0; // Save
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
            if (key == GLFW_KEY_EQUAL) gCmdQueue.push(AppCmd::FontIncrease);
            if (key == GLFW_KEY_MINUS) gCmdQueue.push(AppCmd::FontDecrease);
        }

        // File shortcuts
        if (cmd) {
            if (key == GLFW_KEY_N && !shift) { gCmdQueue.push(AppCmd::FileNew); return; }
            if (key == GLFW_KEY_O && !shift) { gCmdQueue.push(AppCmd::FileOpen); return; }
            if (key == GLFW_KEY_S && !shift) { gCmdQueue.push(AppCmd::FileSave); return; }
            if (key == GLFW_KEY_S && shift)  { gCmdQueue.push(AppCmd::FileSaveAs); return; }
            if (key == GLFW_KEY_W && !shift) { gCmdQueue.push(AppCmd::FileClose); return; }
            if (key == GLFW_KEY_K && !shift) { gCmdQueue.push(AppCmd::EditClearOutput); return; }
        }

        // Cmd+Arrow: cursor movement (bypass ImGui key routing)
        if (cmd) {
            if (key == GLFW_KEY_LEFT)  { gCmdQueue.push(AppCmd::MoveHome, shift); return; }
            if (key == GLFW_KEY_RIGHT) { gCmdQueue.push(AppCmd::MoveEnd, shift); return; }
            if (key == GLFW_KEY_UP)    { gCmdQueue.push(AppCmd::MoveTop, shift); return; }
            if (key == GLFW_KEY_DOWN)  { gCmdQueue.push(AppCmd::MoveBottom, shift); return; }
        }

        // Find shortcuts
        if (cmd) {
            if (key == GLFW_KEY_F && !shift) { gCmdQueue.push(AppCmd::FindShow); return; }
            if (key == GLFW_KEY_G && !shift) { gCmdQueue.push(AppCmd::FindNext); return; }
            if (key == GLFW_KEY_G && shift)  { gCmdQueue.push(AppCmd::FindPrevious); return; }
            if (key == GLFW_KEY_E && !shift) { gCmdQueue.push(AppCmd::FindUseSelection); return; }
            if (key == GLFW_KEY_E && shift)  { gCmdQueue.push(AppCmd::FindUseSelectionReplace); return; }
        }

        // Eval shortcuts
        if (key == GLFW_KEY_ENTER) {
            if (cmd && shift) { gCmdQueue.push(AppCmd::EvalFile); return; }
            if (cmd)          { gCmdQueue.push(AppCmd::EvalSelection); return; }
            if (shift)        { gCmdQueue.push(AppCmd::EvalLine); return; }
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
    // NOTE: NavEnableKeyboard intentionally omitted -- arrow keys are for
    // text editing, not widget navigation.  Enabling it causes the nav
    // system to consume arrow‑key presses and move focus between panes.

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

    applyAppTheme(themeDark);

    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplMetal_Init(device);

    // macOS convention: Ctrl+click acts as a right-click. AppKit does
    // this translation but GLFW bypasses it, so reproduce it here --
    // replace ImGui's installed callback with one that translates the
    // whole press/release pair, then hands the event to the backend.
    glfwSetMouseButtonCallback(
        window, [](GLFWwindow* w, int button, int action, int mods) {
            static bool ctrlClickAsRight = false;
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL))
                    ctrlClickAsRight = true;
                if (ctrlClickAsRight) {
                    button = GLFW_MOUSE_BUTTON_RIGHT;
                    if (action == GLFW_RELEASE) ctrlClickAsRight = false;
                }
            }
            ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
        });

    // --- GUI state and panels ----------------------------------------------
    GuiState guiState;
    guiState.asyncEval.onFinished = [] { glfwPostEmptyEvent(); };

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
    ControlsPanel controlsPanel;
    NotebookPanel notebookPanel;
    PluginBrowserPanel pluginBrowserPanel;
    // Which pane the center shows while a notebook is open. Toggled by
    // View > Toggle Notebook / Editor (Cmd+\); the notebook document
    // stays alive (widgets, history, queued runs) while hidden.
    bool notebookVisible = true;

    guiState.output.append("Tzopilotl. Cmd+Enter: eval block, "
                           "Shift+Enter: eval line, "
                           "Cmd+Shift+Enter: eval file.", LineKind::Info);

    // Open the startup document from the command line: a .tzd opens in the
    // notebook (main() never evaluates it); a .x was already evaluated there
    // and is also shown in an editor tab.
    if (!appCtx.startupDocument.empty()) {
        std::string const& path = appCtx.startupDocument;
        if (hasExtension(path, "tzd")) {
            std::string err;
            if (notebookPanel.open(path, appCtx, err)) {
                notebookVisible = true;
                guiState.output.append("opened " + path, LineKind::Info);
            } else {
                guiState.output.append("notebook open failed: " + err,
                                       LineKind::Error);
            }
        } else {
            workspacePanel.openFile(path);
        }
    } else {
        // No startup document: come up in notebook mode with a fresh
        // untitled document.
        notebookPanel.newDocument();
    }

    // --- Install GLFW callbacks --------------------------------------------
    glfwSetWindowCloseCallback(window, windowCloseCallback);
    gPrevScrollCallback = glfwSetScrollCallback(window, scrollCallback);
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
            // Handle pending close-tab confirmation. The editor panel defers
            // closing a modified tab so we can show the native dialog here.
            if (auto* editor = workspacePanel.editorWithPendingClose()) {
                int idx = editor->pendingCloseIndex();
                std::string name = editor->pendingCloseName();
                bool hasPath = editor->pendingCloseHasPath();
                int choice = showCloseTabAlert(name);
                editor->clearPendingClose();
                if (choice == 0) {
                    bool saved = false;
                    if (hasPath) {
                        saved = editor->saveTab(idx);
                    } else {
                        std::string path = nativeSaveFileDialog(name);
                        if (!path.empty()) saved = editor->saveTabAs(idx, path);
                    }
                    if (saved) editor->closeTab(idx);
                } else if (choice == 1) {
                    editor->closeTab(idx);
                }
                // Clear stale eval state set during the modal
                guiState.evalFile = guiState.evalSelection
                                  = guiState.evalLine = false;
                gCmdQueue.purgeEvals();
                glfwFocusWindow(window);
                [nswin makeFirstResponder:nswin.contentView];
            }

            // Handle quit request (Cmd+Q or window close button)
            if (gWantsToQuit) {
                gWantsToQuit = false;
                auto unsaved = workspacePanel.unsavedFileNames();
                if (notebookPanel.isOpen() && notebookPanel.modified()) {
                    unsaved.push_back(notebookPanel.filePath().empty()
                        ? std::string("untitled notebook")
                        : notebookPanel.filePath());
                }
                if (unsaved.empty()) break;
                int choice = showUnsavedChangesAlert(unsaved);
                if (choice == 0) { workspacePanel.saveAll(); break; } // Save All
                if (choice == 1) break;                               // Don't Save
                // Cancel -- clear stale eval state from modal dialog
                guiState.evalFile = guiState.evalSelection
                                  = guiState.evalLine = false;
                gCmdQueue.purgeEvals();
                glfwFocusWindow(window);
                [nswin makeFirstResponder:nswin.contentView];
            }

            // Throttle frame rate when idle to save CPU.
            // Use short timeout when animations or async work are active,
            // or when widget events are pending (e.g. a callback deferred
            // because an eval holds the VM mutex).
            bool needsActivity = guiState.flash.active()
                              || guiState.asyncEval.busy()
                              || controlsPanel.wantsContinuousFrames()
                              || notebookPanel.hasQueuedRuns()
                              || guiState.printCapture.hasPending()
                              || gDeferredEditFrame != 0
                              || (appCtx.uiState
                                  && controlsPanel.hasPendingEvents(*appCtx.uiState));
            if (needsActivity)
                glfwWaitEventsTimeout(1.0 / 30.0);
            else
                glfwWaitEventsTimeout(0.5);

            // Drain captured print output. Lines captured while a notebook
            // cell's eval is in flight are attributed to that cell; all
            // other prints (actors, timers, engine) go to the console.
            {
                std::uint64_t evalCell =
                    (guiState.asyncEval.busy() || guiState.asyncEval.finished())
                        ? guiState.asyncEval.cellId : 0;
                auto lines = guiState.printCapture.drainLines();
                if (evalCell != 0 && notebookPanel.isOpen()) {
                    for (auto& line : lines)
                        notebookPanel.addCellOutput(evalCell, line,
                                                    LineKind::Output);
                } else {
                    for (auto& line : lines)
                        guiState.output.append(line, LineKind::Output);
                }
            }

            // Collect async eval results: notebook cell evals route to the
            // cell; editor evals format into the console as before.
            if (guiState.asyncEval.finished() && guiState.asyncEval.cellId != 0) {
                guiState.asyncEval.join();
                notebookPanel.onEvalDone(guiState.asyncEval.cellId,
                                         guiState.asyncEval.result,
                                         guiState.asyncEval.code);
                guiState.asyncEval.cellId = 0;
            } else {
                guiState.asyncEval.collect(guiState);
            }

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

            // Inject deferred key events for text-field clipboard operations
            if (gDeferredEditFrame == 1) {
                io.AddKeyEvent(ImGuiMod_Super, true);
                if (gDeferredEditShift) io.AddKeyEvent(ImGuiMod_Shift, true);
                io.AddKeyEvent(gDeferredEditKey, true);
                gDeferredEditFrame = 2;
            } else if (gDeferredEditFrame == 2) {
                io.AddKeyEvent(gDeferredEditKey, false);
                if (gDeferredEditShift) io.AddKeyEvent(ImGuiMod_Shift, false);
                io.AddKeyEvent(ImGuiMod_Super, false);
                gDeferredEditKey = ImGuiKey_None;
                gDeferredEditShift = false;
                gDeferredEditFrame = 0;
            }

            ImGui::NewFrame();

            // A gesture widget hovered last frame owns the keyboard:
            // claim its typed characters before any editor draws.
            uiHoverKeysNewFrame();

            // ---------------------------------------------------------------
            // Dispatch queued commands (from native menus and key shortcuts)
            // ---------------------------------------------------------------
            auto& editorPanel = workspacePanel.activeEditor();

            auto applyFontIdx = [&](int idx) {
                idx = std::max(0, std::min(idx, numFontSizes - 1));
                if (idx != currentFontIdx) {
                    currentFontIdx = idx;
                    io.FontDefault = fonts[currentFontIdx];
                }
            };
            // Defer clipboard keys when an ImGui text field is focused (e.g.
            // Find/Replace bar) so ImGui handles them next frame instead of
            // the editor.
            auto deferKey = [](ImGuiKey key, bool shift = false) {
                gDeferredEditKey = key;
                gDeferredEditShift = shift;
                gDeferredEditFrame = 1;
            };
            // Native dialogs pump macOS events, so eval shortcuts can fire
            // during them; drop stale eval state and restore keyboard focus
            // after a dialog returns.
            auto afterNativeDialog = [&]() {
                guiState.evalFile = guiState.evalSelection = guiState.evalLine = false;
                gCmdQueue.purgeEvals();
                glfwFocusWindow(window);
                [nswin makeFirstResponder:nswin.contentView];
            };
            auto doSaveAs = [&]() {
                std::string path = nativeSaveFileDialog(
                    editorPanel.activeTabName());
                if (!path.empty())
                    editorPanel.saveAs(path);
                afterNativeDialog();
            };
            // Save the notebook; asks for a path if it has none (or if
            // forceDialog). Returns false if cancelled or failed.
            auto saveNotebook = [&](bool forceDialog) {
                std::string path = notebookPanel.filePath();
                if (path.empty() || forceDialog) {
                    path = nativeSaveFileDialog("untitled.tzd", "tzd");
                    afterNativeDialog();
                    if (path.empty()) return false;
                }
                std::string err;
                if (!notebookPanel.saveAs(path, appCtx, err)) {
                    guiState.output.append("notebook save failed: " + err,
                                           LineKind::Error);
                    return false;
                }
                guiState.output.append("saved " + path, LineKind::Info);
                return true;
            };
            // Replacing the open notebook (New Notebook / opening a .tzd)
            // discards it. Ask first when it has unsaved changes; false
            // means "user cancelled, keep the current document".
            auto confirmDiscardNotebook = [&]() {
                if (!notebookPanel.isOpen() || !notebookPanel.modified())
                    return true;
                std::string name = notebookPanel.filePath().empty()
                    ? std::string("untitled notebook")
                    : notebookPanel.filePath();
                int choice = showCloseTabAlert(name);
                bool proceed = choice == 0   ? saveNotebook(false)
                             : choice == 2   ? false
                                             : true;  // 1 = Don't Save
                afterNativeDialog();
                return proceed;
            };
            auto openNotebook = [&](std::string const& path) {
                auto oldPanels = notebookPanel.claimedPanels();
                std::string err;
                if (notebookPanel.open(path, appCtx, err)) {
                    // The replaced document's widgets go with it -- but
                    // keep panels the new document also claims (its load
                    // just restored their widgets).
                    auto newPanels = notebookPanel.claimedPanels();
                    std::vector<std::string> stale;
                    for (auto const& p : oldPanels) {
                        bool kept = false;
                        for (auto const& n : newPanels)
                            if (n == p) { kept = true; break; }
                        if (!kept) stale.push_back(p);
                    }
                    controlsPanel.queuePanelRemoval(stale);
                    notebookVisible = true;
                    guiState.output.append("opened " + path, LineKind::Info);
                } else {
                    guiState.output.append("notebook open failed: " + err,
                                           LineKind::Error);
                }
            };

            for (AppCommand const& c : gCmdQueue.drain()) {
                switch (c.cmd) {
                case AppCmd::Quit:
                    gWantsToQuit = true;
                    break;

                // -- File ---------------------------------------------------
                case AppCmd::FileNew:
                    editorPanel.newTab();
                    break;
                case AppCmd::FileNewNotebook:
                    // Only one notebook is open at a time: starting a new
                    // one replaces the current document.
                    if (confirmDiscardNotebook()) {
                        // The discarded document's widgets go with it.
                        controlsPanel.queuePanelRemoval(
                            notebookPanel.claimedPanels());
                        notebookPanel.newDocument();
                        notebookVisible = true;
                    }
                    break;
                case AppCmd::FileOpen: {
                    std::string path = nativeOpenFileDialog();
                    if (!path.empty()) {
                        if (std::filesystem::is_directory(path))
                            workspacePanel.addWorkspace(path);
                        else if (hasExtension(path, "tzd")) {
                            // Opening a notebook replaces the current one.
                            if (confirmDiscardNotebook()) openNotebook(path);
                        }
                        else if (notebookPanel.isOpen() && notebookVisible
                                 && notebookPanel.openFileIntoFocusedCell(path))
                            ;  // read into the focused code cell
                        else
                            workspacePanel.openFile(path);
                    }
                    afterNativeDialog();
                    break;
                }
                case AppCmd::FileOpenFolder: {
                    std::string path = nativeOpenFolderDialog();
                    if (!path.empty() && std::filesystem::is_directory(path))
                        workspacePanel.addWorkspace(path);
                    afterNativeDialog();
                    break;
                }
                case AppCmd::FileSave:
                    if (notebookPanel.isOpen() && notebookVisible)
                        saveNotebook(false);
                    else if (editorPanel.hasFilePath())
                        editorPanel.save();
                    else
                        doSaveAs();
                    break;
                case AppCmd::FileSaveAs:
                    if (notebookPanel.isOpen() && notebookVisible)
                        saveNotebook(true);
                    else
                        doSaveAs();
                    break;
                case AppCmd::FileSaveCopy: {
                    std::string path = nativeSaveFileDialog(
                        editorPanel.activeTabName());
                    if (!path.empty())
                        editorPanel.saveCopy(path);
                    afterNativeDialog();
                    break;
                }
                case AppCmd::FileClose:
                    if (notebookPanel.isOpen() && notebookVisible) {
                        bool doClose = true;
                        if (notebookPanel.modified()) {
                            std::string name = notebookPanel.filePath().empty()
                                ? std::string("untitled notebook")
                                : notebookPanel.filePath();
                            int choice = showCloseTabAlert(name);
                            if (choice == 0) doClose = saveNotebook(false);
                            else if (choice == 2) doClose = false;
                            afterNativeDialog();
                        }
                        if (doClose) {
                            // The closed document's widgets go with it.
                            controlsPanel.queuePanelRemoval(
                                notebookPanel.claimedPanels());
                            notebookPanel.close();
                        }
                    } else {
                        editorPanel.closeActiveTab();
                    }
                    break;

                // -- Edit ---------------------------------------------------
                // Undo/redo are focus-routed: a focused text field (cell
                // editor, find bar) gets the key injected and handles its
                // own fine-grained undo; otherwise the open notebook's
                // document history (or the editor panel) takes it.
                // io.WantTextInput here means a real ImGui text field
                // (find bar, panel name) is active -- it keeps the flag
                // through NewFrame, so the injected key reaches it. Cell
                // TextEditors get DIRECT calls on the focused cell
                // instead: no injection, no focus-timing dependence.
                case AppCmd::EditUndo:
                    if (io.WantTextInput) deferKey(ImGuiKey_Z);
                    else if (notebookPanel.isOpen() && notebookVisible) {
                        // Character undo while the cursor is in a cell's
                        // text; document history otherwise (guide 4.6).
                        if (!(gTextFieldFocusedLastFrame
                              && notebookPanel.undoText()))
                            notebookPanel.undoDocument(appCtx);
                    } else editorPanel.undo();
                    break;
                case AppCmd::EditRedo:
                    if (io.WantTextInput) deferKey(ImGuiKey_Z, true);
                    else if (notebookPanel.isOpen() && notebookVisible) {
                        if (!(gTextFieldFocusedLastFrame
                              && notebookPanel.redoText()))
                            notebookPanel.redoDocument(appCtx);
                    } else editorPanel.redo();
                    break;
                case AppCmd::EditCut:
                    if (io.WantTextInput) deferKey(ImGuiKey_X);
                    else if (notebookPanel.isOpen() && notebookVisible)
                        notebookPanel.cutText();
                    else editorPanel.cut();
                    break;
                case AppCmd::EditCopy:
                    if (io.WantTextInput) deferKey(ImGuiKey_C);
                    else if (outputPanel.tryCopy()) {}
                    else if (notebookPanel.isOpen() && notebookVisible)
                        notebookPanel.copyText();
                    else editorPanel.copy();
                    break;
                case AppCmd::EditPaste:
                    if (io.WantTextInput) deferKey(ImGuiKey_V);
                    else if (notebookPanel.isOpen() && notebookVisible)
                        notebookPanel.pasteText();
                    else editorPanel.paste();
                    break;
                case AppCmd::EditSelectAll:
                    if (io.WantTextInput) deferKey(ImGuiKey_A);
                    else if (outputPanel.trySelectAll()) {}
                    else if (notebookPanel.isOpen() && notebookVisible)
                        notebookPanel.selectAllText();
                    else editorPanel.selectAll();
                    break;
                case AppCmd::EditClearOutput:
                    outputPanel.clear(guiState.output);
                    break;
                case AppCmd::EditToggleComment:
                    editorPanel.toggleComment();
                    break;
                case AppCmd::EditIndent:
                    editorPanel.indent();
                    break;
                case AppCmd::EditOutdent:
                    editorPanel.outdent();
                    break;

                // -- Cursor movement (Cmd+Arrow), routed to focused pane -----
                case AppCmd::MoveHome:
                    if (outputPanel.hasFocus()) outputPanel.moveHome(c.shift);
                    else if (notebookPanel.isOpen() && notebookVisible)
                        notebookPanel.moveHome(c.shift);
                    else editorPanel.moveHome(c.shift);
                    break;
                case AppCmd::MoveEnd:
                    if (outputPanel.hasFocus()) outputPanel.moveEnd(c.shift);
                    else if (notebookPanel.isOpen() && notebookVisible)
                        notebookPanel.moveEnd(c.shift);
                    else editorPanel.moveEnd(c.shift);
                    break;
                case AppCmd::MoveTop:
                    if (outputPanel.hasFocus()) outputPanel.moveTop(c.shift);
                    else if (notebookPanel.isOpen() && notebookVisible)
                        notebookPanel.moveTop(c.shift);
                    else editorPanel.moveTop(c.shift);
                    break;
                case AppCmd::MoveBottom:
                    if (outputPanel.hasFocus()) outputPanel.moveBottom(c.shift);
                    else if (notebookPanel.isOpen() && notebookVisible)
                        notebookPanel.moveBottom(c.shift);
                    else editorPanel.moveBottom(c.shift);
                    break;

                // -- Find ---------------------------------------------------
                case AppCmd::FindShow: {
                    auto& fr = editorPanel.findReplace();
                    std::string sel = editorPanel.getSelectedText();
                    if (!sel.empty())
                        fr.useSelectionForFind(sel);
                    fr.show();
                    if (auto* ed = editorPanel.activeEditor()) {
                        fr.search(*ed);
                        editorPanel.updateSearchHighlights();
                    }
                    break;
                }
                case AppCmd::FindNext:
                    if (auto* ed = editorPanel.activeEditor()) {
                        editorPanel.findReplace().findNext(*ed);
                        editorPanel.updateSearchHighlights();
                    }
                    break;
                case AppCmd::FindPrevious:
                    if (auto* ed = editorPanel.activeEditor()) {
                        editorPanel.findReplace().findPrevious(*ed);
                        editorPanel.updateSearchHighlights();
                    }
                    break;
                case AppCmd::FindUseSelection: {
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
                    break;
                }
                case AppCmd::FindUseSelectionReplace: {
                    std::string sel = editorPanel.getSelectedText();
                    if (!sel.empty()) {
                        editorPanel.findReplace().useSelectionForReplace(sel);
                        editorPanel.findReplace().show();
                    }
                    break;
                }

                // -- View ---------------------------------------------------
                case AppCmd::FontIncrease:
                    applyFontIdx(currentFontIdx + 1);
                    break;
                case AppCmd::FontDecrease:
                    applyFontIdx(currentFontIdx - 1);
                    break;
                case AppCmd::FontSet:
                    applyFontIdx(c.arg);
                    break;
                case AppCmd::ThemeSet:
                    applyAppTheme(c.arg);
                    break;
                case AppCmd::ToggleNotebookView:
                    if (notebookPanel.isOpen())
                        notebookVisible = !notebookVisible;
                    break;
                case AppCmd::TogglePluginBrowser:
                    pluginBrowserPanel.toggle();
                    break;

                // -- Eval (flags processed after draw, when the ImGui tab
                //    bar has committed the visually selected tab) -----------
                case AppCmd::EvalSelection:
                    guiState.evalSelection = true;
                    break;
                case AppCmd::EvalLine:
                    guiState.evalLine = true;
                    break;
                case AppCmd::EvalFile:
                    guiState.evalFile = true;
                    break;
                }
            }

            // ---------------------------------------------------------------
            // Layout: optional sidebar | editor + output with splitters
            // (Eval requests processed AFTER draw so ImGui tab bar has
            //  updated activeTab_ to match the visually selected tab.)
            // ---------------------------------------------------------------
            controlsPanel.beginFrame();
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

            // Notebook replaces the editor pane while a document is open
            // and shown (View > Toggle Notebook / Editor swaps back).
            if (notebookPanel.isOpen() && notebookVisible) {
                // Notebook view: the console is a RIGHT column -- the
                // cell strip wants the full window height.
                float vsplitW = 6.0f;
                float nbW = std::clamp(
                    (rightW - vsplitW) * guiState.notebookSplitRatio,
                    240.0f, rightW - vsplitW - 160.0f);
                notebookPanel.draw(nbW, totalH, guiState, appCtx,
                                   session, controlsPanel);
                // Zero spacing around the splitter: the seam between the
                // cell strip and the console is just the 6px grab bar.
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::InvisibleButton("##nbsplitter",
                                       ImVec2(vsplitW, totalH));
                if (ImGui::IsItemActive()) {
                    guiState.notebookSplitRatio = std::clamp(
                        guiState.notebookSplitRatio
                            + io.MouseDelta.x / (rightW - vsplitW),
                        0.3f, 0.9f);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                ImGui::SameLine(0.0f, 0.0f);
                outputPanel.draw(rightW - vsplitW - nbW, totalH,
                                 guiState.output);
            } else {
                editorPanel.draw(rightW, editorH, guiState);

                // Horizontal splitter
                ImGui::InvisibleButton("##splitter",
                                       ImVec2(rightW, splitterH));
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
            }

            ImGui::EndGroup();

            // ---------------------------------------------------------------
            // Process eval requests AFTER draw (activeTab_ is now current).
            // With a notebook open the shortcuts drive cells instead:
            // Cmd+Enter / Shift+Enter run the focused cell, Cmd+Shift+Enter
            // runs all cells.
            // ---------------------------------------------------------------
            if (notebookPanel.isOpen())
                notebookPanel.pumpRunQueue(guiState, appCtx, session);
            if (notebookPanel.isOpen() && notebookVisible) {
                if (guiState.evalSelection || guiState.evalLine) {
                    guiState.evalSelection = guiState.evalLine = false;
                    notebookPanel.runFocused(guiState, appCtx, session);
                }
                if (guiState.evalFile) {
                    guiState.evalFile = false;
                    notebookPanel.runAll();
                }
            } else if (session && !guiState.asyncEval.busy()) {
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

            // ---------------------------------------------------------------
            // Live control widgets (`ui` module): draw floating panel
            // windows (skipping panels rendered inline as notebook panel
            // cells), then dispatch value changes -- engine fast path in
            // one bundle per silo, lang callbacks coalesced under try_lock.
            // (controlsPanel.beginFrame() ran before the notebook drew its
            // panel cells, so their noteTapsVisible reports survive.)
            // ---------------------------------------------------------------
            if (appCtx.uiState) {
                // Key-bound widgets (ui.bindKey) fire from here whenever
                // no text field owns the keyboard -- notebook, perform
                // mode, and floating panels alike.
                dispatchWidgetKeys(*appCtx.uiState);
                // Panels claimed by the open notebook NEVER float -- not
                // while the notebook is hidden either (closing a floating
                // window deletes widgets, a trap when they were only
                // borrowed from the document). Hidden notebook = its
                // controls are simply not shown until it returns.
                auto claimed = notebookPanel.isOpen()
                             ? notebookPanel.claimedPanels()
                             : std::vector<std::string>{};
                controlsPanel.draw(*appCtx.uiState,
                                   claimed.empty() ? nullptr : &claimed);
                controlsPanel.dispatch(*appCtx.uiState, appCtx);
            }

            pluginBrowserPanel.draw(appCtx);

            // Notebook history upkeep: widget gesture commits, typing
            // coalesce, eval/structure commits (after dispatch so committed
            // values are the ones just sent).
            notebookPanel.update(appCtx);

            // TZPL_APP_DEMO=quit-dirty-last: headless repro (mirrors the
            // JUCE demo) -- edit the last cell ~2s in, request quit ~1s
            // later; the unsaved-changes alert must appear (the app then
            // stays alive behind the modal instead of exiting).
            static char const* kAppDemo = std::getenv("TZPL_APP_DEMO");
            if (kAppDemo && std::strcmp(kAppDemo, "quit-dirty-last") == 0) {
                static int demoStage = 0;
                static double demoT0 = ImGui::GetTime();
                double t = ImGui::GetTime() - demoT0;
                if (demoStage == 0 && t > 2.0) {
                    bool ok = notebookPanel.testEditLastCell("play(5.0)",
                                                             "play(5.1)");
                    fprintf(stderr,
                            "app-demo quit-dirty-last: edited=%d modified=%d\n",
                            ok ? 1 : 0, notebookPanel.modified() ? 1 : 0);
                    demoStage = 1;
                } else if (demoStage == 1 && t > 3.0) {
                    fprintf(stderr,
                            "app-demo quit-dirty-last: quitting, modified=%d\n",
                            notebookPanel.modified() ? 1 : 0);
                    gWantsToQuit = true;
                    demoStage = 2;
                }
            }

            // Toolbar Close button routes through the same confirm-and-
            // close flow as Cmd+W (handled next frame by dispatch).
            if (notebookPanel.takeCloseRequest())
                gCmdQueue.push(AppCmd::FileClose);
            if (notebookPanel.takeEditorViewRequest())
                notebookVisible = false;

            // Mirror unsaved work into the close box (macOS documentEdited
            // dot), matching the quit prompt: a dirty notebook or editor
            // tab counts.
            {
                bool edited = workspacePanel.hasUnsavedChanges()
                           || (notebookPanel.isOpen()
                               && notebookPanel.modified());
                if (edited != nswin.documentEdited)
                    [nswin setDocumentEdited:edited];
            }

            // All windows have drawn: WantTextInput now reflects any
            // focused text field, including the cell TextEditors that
            // only assert it during their draw. Snapshot for next
            // frame's edit-command routing.
            gTextFieldFocusedLastFrame = io.WantTextInput;

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
