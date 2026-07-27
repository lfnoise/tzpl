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
//  main_component.hpp
//  app (JUCE)
//
//  The window's content: editor tabs over the output console with a
//  draggable split, plus the ApplicationCommandTarget handling every app
//  command, the REPL session, and the async eval plumbing.
//

#ifndef main_component_hpp
#define main_component_hpp

#include "app_commands.hpp"
#include "editor_pane.hpp"
#include "output_console.hpp"
#include "notebook/notebook_view.hpp"
#include "widgets/controls_dispatch.hpp"
#include "widgets/controls_window.hpp"
#include "widgets/key_dispatch.hpp"
#include "plugin_browser_window.hpp"
#include "perform_view.hpp"
#include "graph_view.hpp"
#include "status_bar.hpp"
#include "gui_state.hpp"
#include "tzpl_look_and_feel.hpp"
#include <map>
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace bridge { struct AppContext; }
namespace ts { class REPLSession; }

namespace tzplapp {

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      private juce::Timer {
public:
    MainComponent(bridge::AppContext& appCtx,
                  juce::ApplicationCommandManager& commands,
                  TzplLookAndFeel& lookAndFeel,
                  juce::PropertiesFile& settings);
    ~MainComponent() override;

    void resized() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& ids) override;
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform(InvocationInfo const& info) override;

    // Append an info line to the output console.
    void logLine(juce::String const& line);

    // Open a .x/.tzd file as if chosen in the Open dialog (also the entry
    // point for Finder open events). Registers the file's project (nearest
    // ancestor with a `tzpl-config` file) so its modules/ joins the search path.
    // Files inside the distribution's examples/ open as untitled copies.
    void openPath(juce::File const& file);

    // Recent projects (most recent first), for the File menu submenu.
    juce::StringArray recentProjectRoots() const;
    void openRecentProject(int index);

    // Asks about unsaved changes (async) and calls `proceed` only if the
    // user did not cancel. With nothing unsaved it proceeds immediately.
    void confirmUnsavedChangesThen(std::function<void()> proceed);

    int currentTheme() const { return lookAndFeel_.currentTheme(); }
    int currentFontIndex() const { return fontIndex_; }

    bool notebookActive() const { return centerMode_ == CenterMode::notebook; }

    // Fresh untitled notebook shown in the center pane. The launch default
    // (when no startup document is given) and File > New Notebook.
    void openNewNotebook();

    // About Tzopilotl: splash artwork + version/license, click to dismiss.
    void showAboutBox();

    // Test hooks for the TZPL_JUCE_SELFTEST / TZPL_JUCE_OPEN paths.
    void testTypeIntoEditor(juce::String const& text);
    bool testEvalCollected() const;
    juce::String testLastEvalSummary() const;
    void testOpenFile(juce::File const& f) {
        registerProjectFor(f);
        if (f.hasFileExtension("tzd")) openNotebookFile(f);
        else editorPane_.openFile(f);
    }
    EditorPane& testEditorPane() { return editorPane_; }
    void testShowDemo(juce::String const& which);
    void testShowNotebook(bool show) { showNotebook(show); }
    NotebookView& testNotebook() { return *notebook_; }
    int testWidgetCount() const;
    // Nudge the selftest's slider ("a" on panel "t") and return its new
    // value (exercises the WidgetComponent->UIState->dispatcher path).
    double testDriveFirstSlider();
    // Presets: notebook capture + recall round-trip for `panel`.
    bool testPresetsRoundTrip(std::string const& panel) {
        return notebook_->testPresetsRoundTrip(panel);
    }
    // Key bindings: simulate a chord press; returns the bound widget's value.
    double testFireKeyChord(std::string const& chord) {
        return keyDispatch_ ? keyDispatch_->testPressChord(chord) : -999.0;
    }

private:
    void applyTheme(int themeIdx);
    void applyFontIndex(int idx);
    void saveSplitRatio();

    void openFileFlow();
    void newProjectFlow();
    void createProject(juce::File const& dir);
    void registerProjectFor(juce::File const& file);
    void noteRecentProject(juce::String const& root, juce::String const& doc);
    void updateSessionDocumentPath();
    juce::File dialogDefaultDir() const;
    // Persist the directory a completed open/save dialog was used in; it
    // seeds dialogDefaultDir() across launches when no document anchors it.
    void rememberDialogDir(juce::File const& chosen);
    juce::File stdlibModulesDir() const;
    juce::File distExamplesDir() const;
    bool isDistExample(juce::File const& file) const;
    void saveActiveFlow(bool forceDialog, std::function<void(bool)> done = {});
    void closeActiveTabFlow();
    // Reload the active editor tab from disk, confirming first if it has
    // unsaved in-memory edits.
    void revertActiveFlow();
    // Asks about the open notebook's unsaved changes (async) before it is
    // replaced; `proceed` runs only if the user saved or discarded.
    void confirmNotebookDiscardThen(std::function<void()> proceed);
    bool openNotebookFile(juce::File const& file);  // false: open failed
    // `done(true)` fires only after a successful save (an untitled notebook
    // asks for a path first, so this can complete long after the call).
    void saveNotebookFlow(bool forceDialog, std::function<void(bool)> done = {});
    // Save every modified editor tab, asking for a path for the untitled
    // ones (one chooser at a time). `done(false)` if any save fails or the
    // user cancels a chooser -- so callers must not treat it as "saved".
    void saveAllTabsThen(std::function<void(bool)> done);
    void saveNextUntitledTabThen(std::function<void(bool)> done);
    void showNotebook(bool show);
    void togglePerform();
    void togglePluginBrowser();
    // Graph view double-click: float a controls window for a live node.
    void openNodeControls(long long nodeID, std::string const& defName, int silo);
    // Graph view context menu: add a scope (or spectrum) on a node outlet to
    // that node's panel, and float it.
    void openNodeTap(long long nodeID, std::string const& defName, int silo,
                     int outlet, bool spectrum);
    // Buffer "load" button: file dialog -> replaceBuffer + waveform row.
    void loadBufferFlow(long long nodeID, int silo, std::string const& panel,
                        long long bufID, std::string const& bufName);

    // Which component fills the center pane. `graph` overlays a *document*
    // mode (editor or notebook): document-targeted flows (save, eval,
    // undo, revert) keep routing to lastDocMode_'s target while the graph
    // is up, and closing the graph returns to it.
    enum class CenterMode { editor, notebook, graph };
    void setCenterMode(CenterMode m);
    // The document mode currently in effect (sees through graph mode).
    bool docModeIsNotebook() const {
        auto m = centerMode_ == CenterMode::graph ? lastDocMode_ : centerMode_;
        return m == CenterMode::notebook;
    }

    void launchEval(juce::String const& code, int flashStart, int flashEnd);
    void collectEvalResult();
    void timerCallback() override; // print-drain coordinator
    // Show a floating window per ui panel not claimed by the open notebook.
    void refreshControlsWindows();

    bridge::AppContext& appCtx_;
    juce::ApplicationCommandManager& commands_;
    TzplLookAndFeel& lookAndFeel_;
    juce::PropertiesFile& settings_;

    GuiState guiState_;
    std::unique_ptr<ts::REPLSession> session_;
    ControlsDispatcher dispatcher_ { appCtx_ };
    std::unique_ptr<KeyDispatch> keyDispatch_;   // ui.bindKey polling

    EditorPane editorPane_;
    std::unique_ptr<NotebookView> notebook_;
    CenterMode centerMode_ = CenterMode::editor;
    CenterMode lastDocMode_ = CenterMode::editor;
    std::unique_ptr<GraphView> graphView_; // lazily created on first entry
    OutputConsole console_;
    // Bottom chrome: DSP load, master level, latching dropout indicator.
    // Not a center-mode component -- it sits below everything and is only
    // hidden in perform mode.
    StatusBar statusBar_;
    juce::StretchableLayoutManager layout_;
    std::unique_ptr<juce::StretchableLayoutResizerBar> resizer_;

    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::map<std::string, std::unique_ptr<ControlsWindow>> controlsWindows_;
    std::unique_ptr<PerformView> performView_;
    std::unique_ptr<PluginBrowserWindow> pluginBrowser_;
    int fontIndex_ = 0;
    // Throttles the 15 Hz timer's disk-mtime polling down to ~once/second.
    int externalCheckTicks_ = 0;
    // Last unsaved-work state pushed to the native window (close-box dot).
    bool documentEditedShown_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}

#endif /* main_component_hpp */
