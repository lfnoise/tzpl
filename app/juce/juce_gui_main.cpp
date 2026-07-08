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
#include "tzpl_app_context.hpp"
#include "tzpl_look_and_feel.hpp"
#include <juce_gui_extra/juce_gui_extra.h>

namespace tzplapp {

// Set by runGui before the application object is created.
static bridge::AppContext* gAppContext = nullptr;

// ---------------------------------------------------------------------------
// Menu bar model: File / Edit / Find / View, all items driven by the
// command manager (shortcuts shown and dispatched automatically).
// ---------------------------------------------------------------------------

class AppMenuModel : public juce::MenuBarModel {
public:
    explicit AppMenuModel(juce::ApplicationCommandManager& commands)
        : commands_(commands) {}

    juce::StringArray getMenuBarNames() override {
        return { "File", "Edit", "Find", "View" };
    }

    juce::PopupMenu getMenuForIndex(int index, juce::String const&) override {
        juce::PopupMenu m;
        switch (index) {
        case 0: // File
            m.addCommandItem(&commands_, cmd::fileNew);
            m.addCommandItem(&commands_, cmd::fileNewNotebook);
            m.addCommandItem(&commands_, cmd::fileOpen);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::fileSave);
            m.addCommandItem(&commands_, cmd::fileSaveAs);
            m.addCommandItem(&commands_, cmd::fileSaveCopy);
            m.addSeparator();
            m.addCommandItem(&commands_, cmd::fileClose);
#if !JUCE_MAC
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
            m.addCommandItem(&commands_, cmd::toggleNotebookView);
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

    void menuItemSelected(int, int) override {} // command items dispatch themselves

private:
    juce::ApplicationCommandManager& commands_;
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

        menuModel_ = std::make_unique<AppMenuModel>(commands_);
#if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(menuModel_.get());
#else
        window_->setMenuBar(menuModel_.get());
#endif

        // The engine/VM come up before the window (runGui is called partway
        // through main()), so the process may not be frontmost; bring it up.
        juce::Process::makeForegroundProcess();
        window_->toFront(true);

        // TZPL_JUCE_OPEN=<path>: open a file in the editor at startup
        // (testing hook -- a plain file argument is *evaluated*, like the
        // ImGui app).
        if (auto* p = std::getenv("TZPL_JUCE_OPEN")) {
            window_->mainComponent()->testOpenFile(juce::File(juce::String(p)));
            // TZPL_JUCE_EVAL=1: evaluate the just-opened file (creates ui
            // widgets etc.) shortly after the window is up.
            if (std::getenv("TZPL_JUCE_EVAL"))
                juce::Timer::callAfterDelay(300, [this] {
                    commands_.invokeDirectly(cmd::evalFile, false);
                });
        }

        // TZPL_JUCE_DEMO=find|flash: open a visual state at startup so it can
        // be screenshotted without injecting global keystrokes.
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
#if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
#else
        if (window_) window_->setMenuBar(nullptr);
#endif
        window_.reset();
        menuModel_.reset();
        appProperties_.saveIfNeeded();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();
    }

    void systemRequestedQuit() override {
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
            if (id == cmd::fileOpen || id == cmd::fileSave
                || id == cmd::fileSaveAs || id == cmd::fileSaveCopy
                || id == cmd::quit || id == cmd::evalSelection
                || id == cmd::evalLine || id == cmd::evalFile)
                continue;
            juce::ApplicationCommandInfo info(id);
            main->getCommandInfo(id, info);
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

    void anotherInstanceStarted(juce::String const&) override {}
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
};

}

#if JUCE_MAC
namespace juce { extern void initialiseNSApplication(); }
#endif

int runGui(bridge::AppContext& appCtx) {
    tzplapp::gAppContext = &appCtx;
    juce::JUCEApplicationBase::createInstance =
        []() -> juce::JUCEApplicationBase* {
            return new tzplapp::TzplApplication();
        };
#if JUCE_MAC
    juce::initialiseNSApplication();
#endif
    int rc = juce::JUCEApplicationBase::main();
    tzplapp::gAppContext = nullptr;
    return rc;
}
