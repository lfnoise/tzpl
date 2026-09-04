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
//  juce_gui_main.cpp
//  app (JUCE)
//
//  GUI mode entry point. main() lives in src/main.cpp (shared with the
//  ImGui app) and calls runGui(appCtx) after the engine, VM, and
//  schedulers are up. runGui hosts a real JUCEApplicationBase via
//  JUCEApplicationBase::main() -- that (rather than a bare dispatch loop)
//  is what wires up the native macOS app menu, routes Cmd+Q through
//  systemRequestedQuit, and runs an orderly shutdown.
//

#include "app_gui.hpp"
#include "app_commands.hpp"
#include "main_component.hpp"
#include "settings_dialog.hpp"
#include "language_settings_dialog.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_look_and_feel.hpp"
#include "BinaryData.h"
#include <juce_gui_extra/juce_gui_extra.h>
#if ! JUCE_MAC
#include "tzpl_client_interface.hpp"  // setSharedInput for the mouse ugens
#include "tzpl_plugin_abi.h"
#endif

namespace tzplapp {

// Set by runGui before the application object is created.
static bridge::AppContext* gAppContext = nullptr;

// ---------------------------------------------------------------------------
// Menu bar model: File / Edit / Find / View, all items driven by the
// command manager (shortcuts shown and dispatched automatically).
// ---------------------------------------------------------------------------

class AppMenuModel : public juce::MenuBarModel {
public:
    AppMenuModel(juce::ApplicationCommandManager& commands,
                 MainComponent* main)
        : commands_(commands), main_(main) {
        // Rebuild the menus (and their check marks) whenever a command's
        // state changes -- e.g. commandStatusChanged() after a font/theme
        // pick. Without this the ticks freeze at their first-built values.
        setApplicationCommandManagerToWatch(&commands_);
    }

    juce::StringArray getMenuBarNames() override {
        return { "File", "Edit", "Find", "View" };
    }

    juce::PopupMenu getMenuForIndex(int index, juce::String const&) override {
        juce::PopupMenu m;
        switch (index) {
        case 0: // File
            m.addCommandItem(&commands_, cmd::fileNew);
            m.addCommandItem(&commands_, cmd::fileNewNotebook);
            m.addCommandItem(&commands_, cmd::fileNewProject);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::fileOpen);
            m.addCommandItem(&commands_, cmd::fileOpenFolder);
            m.addCommandItem(&commands_, cmd::fileOpenExample);
            if (main_) {
                juce::PopupMenu recent;
                auto roots = main_->recentProjectRoots();
                for (int i = 0; i < roots.size(); ++i)
                    recent.addItem(cmd::recentProjectBase + i, roots[i]);
                m.addSubMenu("Open Recent Project", recent,
                             roots.size() > 0);
            }
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::fileSave);
            m.addCommandItem(&commands_, cmd::fileSaveAs);
            m.addCommandItem(&commands_, cmd::fileSaveCopy);
            m.addCommandItem(&commands_, cmd::fileRevert);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::fileRevealModules);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::fileClose);
#if !JUCE_MAC
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::engineSettings);
            m.addCommandItem(&commands_, cmd::languageSettings);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::helpAbout);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::quit);
#endif
            break;
        case 1: // Edit
            m.addCommandItem(&commands_, cmd::editUndo);
            m.addCommandItem(&commands_, cmd::editRedo);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::editCut);
            m.addCommandItem(&commands_, cmd::editCopy);
            m.addCommandItem(&commands_, cmd::editPaste);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::editSelectAll);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::editClearOutput);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::editToggleComment);
            m.addCommandItem(&commands_, cmd::editIndent);
            m.addCommandItem(&commands_, cmd::editOutdent);
            break;
        case 2: // Find
            m.addCommandItem(&commands_, cmd::findShow);
            m.addCommandItem(&commands_, cmd::findNext);
            m.addCommandItem(&commands_, cmd::findPrevious);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::findUseSelection);
            m.addCommandItem(&commands_, cmd::findUseSelectionReplace);
            break;
        case 3: { // View
            m.addCommandItem(&commands_, cmd::viewEditor);
            m.addCommandItem(&commands_, cmd::viewNotebook);
            m.addCommandItem(&commands_, cmd::viewGraph);
            m.addCommandItem(&commands_, cmd::viewRotate);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::toggleSidebar);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::togglePerform);
            m.addCommandItem(&commands_, cmd::togglePluginBrowser);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::windowCycle);
            m.addCommandItem(&commands_, cmd::windowCycleBack);
            m.addSeparator();
            juce::PopupMenu fontMenu;
            for (int i = 0; i < cmd::kNumEditorFontSizes; ++i)
                fontMenu.addCommandItem(&commands_, cmd::fontSetBase + i);
            m.addSubMenu("Font Size", fontMenu);
            juce::PopupMenu themeMenu;
            for (int i = 0; i < themeCount; ++i)
                themeMenu.addCommandItem(&commands_, cmd::themeSetBase + i);
            m.addSubMenu("Theme", themeMenu);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::fontIncrease);
            m.addCommandItem(&commands_, cmd::fontDecrease);
            break;
        }
        }
        return m;
    }

    // Command items dispatch themselves; only the raw-ID recent-projects
    // entries land here.
    void menuItemSelected(int itemID, int) override {
        if (main_ && itemID >= cmd::recentProjectBase
            && itemID < cmd::recentProjectBase + cmd::kMaxRecentProjects)
            main_->openRecentProject(itemID - cmd::recentProjectBase);
    }

private:
    juce::ApplicationCommandManager& commands_;
    MainComponent* main_ = nullptr;
};

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(bridge::AppContext& appCtx,
               juce::ApplicationCommandManager& commands,
               TzplLookAndFeel& lookAndFeel,
               juce::PropertiesFile& settings)
        : juce::DocumentWindow(
              "Tzopilotl",
              lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId),
              juce::DocumentWindow::allButtons),
          settings_(settings)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);

        auto* main = new MainComponent(appCtx, commands, lookAndFeel, settings);
        setContentOwned(main, false);

        centreWithSize(1280, 800);
        juce::String state = settings.getValue("windowState");
        if (state.isNotEmpty())
            restoreWindowStateFromString(state);

        commands.registerAllCommandsForTarget(main);
        commands.setFirstCommandTarget(main);
        addKeyListener(commands.getKeyMappings());

        setVisible(true);
    }

    ~MainWindow() override {
        settings_.setValue("windowState", getWindowStateAsString());
    }

    MainComponent* mainComponent() {
        return static_cast<MainComponent*>(getContentComponent());
    }

    void closeButtonPressed() override {
        juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
    }

private:
    juce::PropertiesFile& settings_;
};

// ---------------------------------------------------------------------------
// Launch splash: the artwork drawn scaled into a fixed-size window (the
// source image is 1536x1024; drawing it down through the graphics context
// keeps it sharp on retina displays). Deletes itself after a short delay
// or on a mouse click.
// ---------------------------------------------------------------------------

class TzplSplash : public juce::SplashScreen {
public:
    TzplSplash()
        : juce::SplashScreen("Tzopilotl", 630, 420, /*useDropShadow=*/true),
          image_(juce::ImageCache::getFromMemory(
              BinaryData::TzopilotlSplash_png,
              BinaryData::TzopilotlSplash_pngSize)) {}

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);
        g.drawImage(image_, getLocalBounds().toFloat(),
                    juce::RectanglePlacement::centred);
    }

private:
    juce::Image image_;
};

#if ! JUCE_MAC
// ---------------------------------------------------------------------------
// Mouse poller (non-Mac)
// ---------------------------------------------------------------------------
// Feeds the mouseX / mouseY / mouseButton ugens. On macOS the CGEvent-based
// poller in main.cpp covers this for both GUIs and --nogui; elsewhere that
// poller compiles to a stub, so the JUCE app polls the primary display's
// normalized mouse state at ~60 Hz into the engine's shared-input table.
class MousePollerTimer : public juce::Timer {
public:
    MousePollerTimer() { startTimer(16); }
    ~MousePollerTimer() override { stopTimer(); }

    void timerCallback() override {
        auto& desktop = juce::Desktop::getInstance();
        auto* display = desktop.getDisplays().getPrimaryDisplay();
        if (!display) return;
        auto area = display->totalArea.toFloat();
        if (area.isEmpty()) return;
        auto pos = desktop.getMainMouseSource().getScreenPosition();
        auto clamp01 = [](float v) {
            return v < 0.f ? 0.f : v > 1.f ? 1.f : v;
        };
        float x = clamp01((pos.x - area.getX()) / area.getWidth());
        // Mouse slots are bottom-up (up = larger); screen y grows downward.
        float y = clamp01(1.f - (pos.y - area.getY()) / area.getHeight());
        bool down = juce::ModifierKeys::getCurrentModifiersRealtime()
                        .isLeftButtonDown();
        engine::setSharedInput(tzpl_sharedMouseX, x);
        engine::setSharedInput(tzpl_sharedMouseY, y);
        engine::setSharedInput(tzpl_sharedMouseButton, down ? 1.f : 0.f);
    }
};
#endif

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

class TzplApplication : public juce::JUCEApplicationBase {
public:
    juce::String const getApplicationName() override { return "Tzopilotl"; }
    juce::String const getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(juce::String const&) override {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Tzopilotl";
        opts.filenameSuffix = ".settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.folderName = "Tzopilotl";
        appProperties_.setStorageParameters(opts);

        lookAndFeel_ = std::make_unique<TzplLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        window_ = std::make_unique<MainWindow>(*gAppContext, commands_,
                                               *lookAndFeel_,
                                               *appProperties_.getUserSettings());

        menuModel_ = std::make_unique<AppMenuModel>(
            commands_, window_->mainComponent());
#if JUCE_MAC
        // JUCE's standard app menu has no About item; add ours at the top.
        juce::PopupMenu appleExtras;
        appleExtras.addCommandItem(&commands_, cmd::helpAbout);
        appleExtras.addSeparator();
        appleExtras.addCommandItem(&commands_, cmd::engineSettings);
        appleExtras.addCommandItem(&commands_, cmd::languageSettings);
        juce::MenuBarModel::setMacMainMenu(menuModel_.get(), &appleExtras);
#else
        window_->setMenuBar(menuModel_.get());
        mousePoller_ = std::make_unique<MousePollerTimer>();
#endif

        // The engine/VM come up before the window (runGui is called partway
        // through main()), so the process may not be frontmost; bring it up.
        juce::Process::makeForegroundProcess();
        window_->toFront(true);

        // Brief launch splash (click dismisses; self-deletes). Skipped for
        // the headless test/demo modes, whose output must not be overlaid.
        if (!std::getenv("TZPL_JUCE_SELFTEST") && !std::getenv("TZPL_JUCE_OPEN")
            && !std::getenv("TZPL_JUCE_DEMO")) {
            auto* splash = new TzplSplash();  // deletes itself
            splash->deleteAfterDelay(juce::RelativeTime::seconds(2.0), true);
        }

        // Startup document from the command line: a .tzd opens in the
        // notebook (main.cpp never evaluates it); a .x was already evaluated
        // there and is also shown in an editor tab.
        if (!gAppContext->startupDocument.empty()) {
            juce::File doc(juce::String(gAppContext->startupDocument));
            if (doc.existsAsFile())
                window_->mainComponent()->openPath(doc);
        } else {
            // No startup document: come up in notebook mode with a fresh
            // untitled document.
            window_->mainComponent()->openNewNotebook();
        }

        // TZPL_JUCE_OPEN=<path>: open a file at startup through the real
        // open path (project registration, example-copy) and report the
        // resulting file association for headless verification. (A plain
        // file argument is *evaluated* instead, like the ImGui app.)
        if (auto* p = std::getenv("TZPL_JUCE_OPEN")) {
            auto* mc = window_->mainComponent();
            mc->openPath(juce::File(juce::String(p)));
            std::printf("JUCE OPEN: tabHasPath=%d nbHasPath=%d nbActive=%d\n",
                        (int)mc->testEditorPane().activeHasFilePath(),
                        (int)(mc->testNotebook().currentFile()
                              != juce::File()),
                        (int)mc->notebookActive());
            std::fflush(stdout);
            // TZPL_JUCE_EVAL=1: evaluate the just-opened file (creates ui
            // widgets etc.) shortly after the window is up.
            if (std::getenv("TZPL_JUCE_EVAL"))
                juce::Timer::callAfterDelay(300, [this] {
                    commands_.invokeDirectly(cmd::evalFile, false);
                });
        }

        // TZPL_JUCE_DEMO=find|flash|graph|settings|history|perform|quit-dirty:
        // open a visual
        // state at startup so it can be screenshotted without injecting
        // global keystrokes.
        if (auto* d = std::getenv("TZPL_JUCE_DEMO"))
            juce::MessageManager::callAsync(
                [this, demo = juce::String(d)] {
                    window_->mainComponent()->testShowDemo(demo);
                });

        // TZPL_JUCE_SELFTEST=1: invoke every registered command through the
        // command manager (as the menus would), report, and quit. Used for
        // headless verification and CI.
        if (std::getenv("TZPL_JUCE_SELFTEST") != nullptr)
            juce::MessageManager::callAsync([this] { runCommandSelfTest(); });
    }

    void shutdown() override {
        // A settings window left open outlives the main window otherwise.
        closeEngineSettings();
        closeLanguageSettings();
#if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
#else
        mousePoller_.reset();
        if (window_) window_->setMenuBar(nullptr);
#endif
        window_.reset();
        menuModel_.reset();
        appProperties_.saveIfNeeded();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();
    }

    void systemRequestedQuit() override {
        if (std::getenv("TZPL_JUCE_DEMO") != nullptr)
            std::fprintf(stderr, "systemRequestedQuit: window=%d main=%d\n",
                         window_ != nullptr ? 1 : 0,
                         (window_ && window_->mainComponent()) ? 1 : 0);
        if (window_ && window_->mainComponent()) {
            window_->mainComponent()->confirmUnsavedChangesThen([] { quit(); });
        } else {
            quit();
        }
    }

    void runCommandSelfTest() {
        auto* main = window_->mainComponent();
        juce::Array<juce::CommandID> ids;
        main->getAllCommands(ids);

        // Theme/font commands apply for real; restore afterwards.
        int themeBefore = main->currentTheme();
        int fontBefore = main->currentFontIndex();

        int invoked = 0, failed = 0;
        for (auto id : ids) {
            // Skip commands that open dialogs or quit (they don't return),
            // and the eval commands (they launch async evals that would
            // race the dedicated eval phases below).
            if (id == cmd::fileOpen || id == cmd::fileOpenFolder
                || id == cmd::fileSave
                || id == cmd::fileSaveAs || id == cmd::fileSaveCopy
                || id == cmd::fileNewProject || id == cmd::fileOpenExample
                || id == cmd::helpAbout || id == cmd::quit || id == cmd::evalSelection
                || id == cmd::evalLine || id == cmd::evalFile
                || id == cmd::togglePerform)  // would overlay the eval phases
                continue;
            juce::ApplicationCommandInfo info(id);
            main->getCommandInfo(id, info);
            // A command that is inactive in the current context (e.g. Revert
            // with no file-backed tab) legitimately returns false from
            // invokeDirectly -- that is not a failure, so skip it.
            if ((info.flags & juce::ApplicationCommandInfo::isDisabled) != 0)
                continue;
            if (commands_.invokeDirectly(id, false)) ++invoked;
            else {
                ++failed;
                std::printf("SELFTEST command failed: %d (%s)\n", (int)id,
                            info.shortName.toRawUTF8());
            }
        }
        commands_.invokeDirectly(cmd::themeSetBase + themeBefore, false);
        commands_.invokeDirectly(cmd::fontSetBase + fontBefore, false);

        std::printf("SELFTEST %s: %d commands invoked, %d failed\n",
                    failed == 0 ? "OK" : "FAILED", invoked, failed);
        std::fflush(stdout);

        // End-to-end evals: type into the editor, fire the Cmd+Enter command,
        // wait for the async REPL round trip, check the result. Phase 0 is a
        // clean eval; phase 1 is a syntax error (exercises error markers).
        // (The command loop left the notebook shown and mutated the active
        // tab via the edit commands -- switch back and start on a clean tab.)
        main->testShowNotebook(false);
        runSaveLineEndingSelfTest();
        runRevertSelfTest();
        main->testEditorPane().newTab("evaltest.x");
        evalPhase_ = 0;
        main->testTypeIntoEditor("40 + 2");
        commands_.invokeDirectly(cmd::evalSelection, false);
        evalPollsLeft_ = 100;
        pollEvalThenQuit();
    }

    void pollEvalThenQuit() {
        auto* main = window_->mainComponent();
        if (main->testEvalCollected()) {
            auto summary = main->testLastEvalSummary();
            if (evalPhase_ == 0) {
                std::printf("SELFTEST EVAL %s: %s\n",
                            summary == "42 : Int" ? "OK" : "FAILED",
                            summary.toRawUTF8());
                std::fflush(stdout);
                evalPhase_ = 1;
                main->testTypeIntoEditor("\n\nlet nope = ;");
                commands_.invokeDirectly(cmd::evalSelection, false);
                evalPollsLeft_ = 100;
            } else if (evalPhase_ == 1) {
                std::printf("SELFTEST ERRMARK %s: %s\n",
                            summary.startsWith("errors:") ? "OK" : "FAILED",
                            summary.toRawUTF8());
                std::fflush(stdout);
                runFindReplaceSelfTest();
                // Notebook eval: fresh document, type into the cell, run it.
                evalPhase_ = 2;
                main->testShowNotebook(true);
                main->testNotebook().newDocument();
                main->testNotebook().testTypeIntoFocusedCell("6 * 7;");
                commands_.invokeDirectly(cmd::evalSelection, false);
                evalPollsLeft_ = 100;
            } else if (evalPhase_ == 2) {
                auto out = main->testNotebook().testFocusedCellOutput();
                std::printf("SELFTEST NOTEBOOK %s: %s\n",
                            out.contains("42") ? "OK" : "FAILED",
                            out.toRawUTF8());
                std::fflush(stdout);
                // Widgets: eval a panel + slider in the editor, then check
                // the registry and drive a slider through the UIState path.
                evalPhase_ = 3;
                main->testShowNotebook(false);
                main->testEditorPane().newTab("widgettest.x");
                main->testTypeIntoEditor(
                    "import ui.*;\npanel(\"t\");\n"
                    "slider(\"a\", 0.0, 1.0, 0.5);\n"
                    "slider(\"b\", 0.0, 10.0, 5.0)");
                commands_.invokeDirectly(cmd::evalFile, false);
                evalPollsLeft_ = 100;
            } else if (evalPhase_ == 3) {
                int n = main->testWidgetCount();
                double v = main->testDriveFirstSlider();
                bool ok = n >= 2 && v == 0.75; // slider "a": map(0.75) on [0,1]
                std::printf("SELFTEST WIDGETS %s: count=%d sliderVal=%.4g\n",
                            ok ? "OK" : "FAILED", n, v);
                std::fflush(stdout);

                // Presets: notebook capture + recall round-trip over panel "t"
                // (synchronous -- reuses the widgets just evaluated).
                bool pOk = main->testPresetsRoundTrip("t");
                std::printf("SELFTEST PRESETS %s: recall restored\n",
                            pOk ? "OK" : "FAILED");
                std::fflush(stdout);

                // Key bindings: bind a key to a toggle, then simulate a press.
                evalPhase_ = 4;
                main->testShowNotebook(false);
                main->testEditorPane().newTab("keytest.x");
                main->testTypeIntoEditor(
                    "import ui.*;\npanel(\"k\");\n"
                    "bindKey(toggle(\"kt\", false), \"g\")");
                commands_.invokeDirectly(cmd::evalFile, false);
                evalPollsLeft_ = 100;
            } else {
                double kv = main->testFireKeyChord("g");
                bool codeOk = KeyDispatch::chordKeyCode("g") == 'G'
                    && KeyDispatch::chordKeyCode("space")
                           == juce::KeyPress::spaceKey
                    && KeyDispatch::chordKeyCode("!") == -1;
                bool ok = kv == 1.0 && codeOk; // toggle flipped false -> true
                std::printf("SELFTEST KEYBIND %s: toggle=%.4g codes=%s\n",
                            ok ? "OK" : "FAILED", kv, codeOk ? "ok" : "bad");
                std::fflush(stdout);
                quit();
                return;
            }
        } else if (--evalPollsLeft_ <= 0) {
            std::printf("SELFTEST EVAL FAILED: timeout (phase %d)\n", evalPhase_);
            std::fflush(stdout);
            quit();
            return;
        }
        juce::Timer::callAfterDelay(100, [this] { pollEvalThenQuit(); });
    }

    void runFindReplaceSelfTest() {
        // Fresh tab, known content; find "foo" and confirm the selection
        // landed on a real occurrence.
        auto& pane = window_->mainComponent()->testEditorPane();
        pane.newTab("findtest.x");
        auto* ed = pane.activeEditor();
        if (ed == nullptr) { std::printf("SELFTEST FIND FAILED: no editor\n"); return; }
        ed->getDocument().replaceAllContent("foo bar foo baz foo");
        ed->moveCaretToTop(false);

        pane.showFind("foo");
        commands_.invokeDirectly(cmd::findNext, false);

        auto sel = ed->getHighlightedRegion();
        juce::String selText = ed->getDocument().getTextBetween(
            juce::CodeDocument::Position(ed->getDocument(), sel.getStart()),
            juce::CodeDocument::Position(ed->getDocument(), sel.getEnd()));
        bool ok = selText == "foo" && sel.getLength() == 3;
        std::printf("SELFTEST FIND %s: selected \"%s\"\n",
                    ok ? "OK" : "FAILED", selText.toRawUTF8());
        std::fflush(stdout);
    }

    // Saving must leave the file LF-only, including the lines the return key
    // typed. JUCE defaults both CodeDocument's newline and replaceWithText's
    // line ending to CRLF, so getting this wrong rewrites a whole source file
    // on the first save.
    void runSaveLineEndingSelfTest() {
        auto& pane = window_->mainComponent()->testEditorPane();
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("tzpl_lineending_selftest.x");
        tmp.replaceWithText("let a = 1;\nlet b = 2;\n", false, false, "\n");

        bool opened = pane.openFile(tmp);
        int idx = pane.activeTabIndex();
        // Type a line the way a user would: the newline via the return key,
        // which is what picks up CodeDocument's newline setting.
        if (auto* ed = pane.activeEditor()) {
            ed->moveCaretToEnd(false);
            ed->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        }
        window_->mainComponent()->testTypeIntoEditor("let c = 3;");
        // The document itself has to stay clean too -- eval runs off this
        // text directly, without a trip through the file.
        bool docNoCR = !pane.getAllText().containsChar('\r');
        bool saved = pane.saveActive();

        auto bytes = tmp.loadFileAsString();
        bool fileNoCR = !bytes.containsChar('\r');
        bool contentOk = bytes.contains("let c = 3;");

        bool ok = opened && saved && docNoCR && fileNoCR && contentOk;
        std::printf("SELFTEST LINEENDINGS %s: saved=%d docNoCR=%d fileNoCR=%d "
                    "content=%d\n", ok ? "OK" : "FAILED", (int)saved,
                    (int)docNoCR, (int)fileNoCR, (int)contentOk);
        std::fflush(stdout);

        pane.closeTab(idx);
        tmp.deleteFile();
    }

    // Revert + external-change detection: open a temp file, edit it on disk
    // behind the app's back, confirm the tab is flagged, then reload it.
    void runRevertSelfTest() {
        auto& pane = window_->mainComponent()->testEditorPane();
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("tzpl_revert_selftest.x");
        tmp.replaceWithText("let a = 1;\n", false, false, "\n");

        bool opened = pane.openFile(tmp);
        int idx = pane.activeTabIndex();
        bool cleanAtOpen = !pane.activeExternallyChanged();

        // Rewrite on disk with a guaranteed-later mtime so the poll sees it
        // regardless of filesystem timestamp granularity.
        tmp.replaceWithText("let a = 2;\n", false, false, "\n");
        tmp.setLastModificationTime(tmp.getLastModificationTime()
                                    + juce::RelativeTime::seconds(2));

        bool detected = pane.checkExternalChanges()
                        && pane.activeExternallyChanged();
        bool reloaded = pane.reloadActive();
        bool contentOk = pane.getAllText().contains("let a = 2;");
        bool flagCleared = !pane.activeExternallyChanged();
        bool notModified = !pane.tabModified(idx);

        bool ok = opened && cleanAtOpen && detected && reloaded && contentOk
                  && flagCleared && notModified;
        std::printf("SELFTEST REVERT %s: detected=%d reloaded=%d content=%d "
                    "cleared=%d\n", ok ? "OK" : "FAILED", detected, reloaded,
                    contentOk, flagCleared);
        std::fflush(stdout);

        pane.closeTab(idx);
        tmp.deleteFile();
    }

    // Finder "open document" events (and second-instance launches) arrive
    // here as a quoted command line. Open every token that is a file.
    void anotherInstanceStarted(juce::String const& commandLine) override {
        if (!window_) return;
        auto* main = window_->mainComponent();
        if (!main) return;
        auto tokens = juce::StringArray::fromTokens(commandLine, true);
        for (auto const& t : tokens) {
            juce::File f(t.unquoted());
            if (f.existsAsFile()) main->openPath(f);
        }
    }
    void suspended() override {}
    void resumed() override {}
    void unhandledException(std::exception const*, juce::String const&,
                            int) override {}

private:
    juce::ApplicationCommandManager commands_;
    juce::ApplicationProperties appProperties_;
    int evalPollsLeft_ = 0;
    int evalPhase_ = 0;
    std::unique_ptr<TzplLookAndFeel> lookAndFeel_;
    std::unique_ptr<AppMenuModel> menuModel_;
    std::unique_ptr<MainWindow> window_;
#if ! JUCE_MAC
    std::unique_ptr<MousePollerTimer> mousePoller_;
#endif
};

}

#if JUCE_MAC
namespace juce { extern void initialiseNSApplication(); }
#endif

namespace tzplapp {

// Register the application factory. MUST run before the first JUCE
// initialisation (the engine's JUCE audio backend holds a
// ScopedJuceInitialiser_GUI and comes up before runGui): JUCE's macOS
// AppDelegate installs itself on NSApp only when isStandaloneApp() --
// i.e. createInstance is non-null -- at MessageManager creation time.
// Without it, terminate: (Cmd+Q, Dock Quit, AppleEvent quit) never
// reaches systemRequestedQuit and the app exits without the
// unsaved-changes prompt. makeJuceAudioBackend() calls this.
void registerJuceAppFactory() {
    juce::JUCEApplicationBase::createInstance =
        []() -> juce::JUCEApplicationBase* {
            return new tzplapp::TzplApplication();
        };
#if JUCE_MAC
    // The delegate lands on NSApp, so NSApp must exist first.
    juce::initialiseNSApplication();
#endif
}

}

int runGui(bridge::AppContext& appCtx) {
    tzplapp::gAppContext = &appCtx;
    tzplapp::registerJuceAppFactory();  // no-op if the backend already did
    int rc = juce::JUCEApplicationBase::main();
    tzplapp::gAppContext = nullptr;
    return rc;
}
