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
//  main_component.cpp
//  app (JUCE)
//

#include "main_component.hpp"
#include "tzpl_app_context.hpp"
#include "nrt_vm.hpp"
#include "repl_session.hpp"
#include "module_compiler.hpp"

namespace tzplapp {

using juce::String;

MainComponent::MainComponent(bridge::AppContext& appCtx,
                             juce::ApplicationCommandManager& commands,
                             TzplLookAndFeel& lookAndFeel,
                             juce::PropertiesFile& settings)
    : appCtx_(appCtx), commands_(commands), lookAndFeel_(lookAndFeel),
      settings_(settings)
{
    addAndMakeVisible(editorPane_);
    addAndMakeVisible(console_);

    notebook_ = std::make_unique<NotebookView>(
        appCtx_, guiState_, [this] { return session_.get(); }, dispatcher_);
    addChildComponent(*notebook_);

    // Horizontal split: center pane | resizer | console column on the right.
    // splitRatio is the editor's width fraction (clamped .2-.9).
    double ratio = settings_.getDoubleValue("splitRatio", 0.7);
    ratio = juce::jlimit(0.2, 0.9, ratio);
    layout_.setItemLayout(0, 120.0, -0.9, -ratio);        // editor
    layout_.setItemLayout(1, 8.0, 8.0, 8.0);              // resizer bar
    layout_.setItemLayout(2, 80.0, -0.8, -(1.0 - ratio)); // console
    resizer_ = std::make_unique<juce::StretchableLayoutResizerBar>(
        &layout_, 1, /*vertical bar=*/true);
    addAndMakeVisible(*resizer_);

    // Redirect VM print output into the capture pipe; the console's timer
    // drains it. Restored in the destructor.
    if (appCtx_.nrtvm && guiState_.printCapture.captureFile())
        appCtx_.nrtvm->vm.setPrintOutput(guiState_.printCapture.captureFile());

    // REPL session for editor evaluation. Reuse the app's ModuleCompiler so
    // modules compiled during the initial runSource() keep their cached
    // type objects (same reasoning as the ImGui app).
    if (appCtx_.nrtvm && appCtx_.compiler) {
        if (appCtx_.moduleCompiler) {
            session_ = std::make_unique<ts::REPLSession>(
                *appCtx_.compiler, appCtx_.nrtvm->vm, appCtx_.target,
                *appCtx_.moduleCompiler);
        } else {
            session_ = std::make_unique<ts::REPLSession>(
                *appCtx_.compiler, appCtx_.nrtvm->vm, appCtx_.target);
        }
    }

    // Wake the message thread when a background eval completes. SafePointer:
    // the callback may land after this component is torn down at quit.
    guiState_.asyncEval.onFinished =
        [safe = juce::Component::SafePointer<MainComponent>(this)]() mutable {
            juce::MessageManager::callAsync([safe] {
                if (safe != nullptr) safe->collectEvalResult();
            });
        };

    applyTheme(settings_.getIntValue("theme", themeDark));
    applyFontIndex(settings_.getIntValue("fontIndex", cmd::kDefaultFontSizeIndex));

    logLine("Tzopilotl. Cmd+Enter: eval block, Shift+Enter: eval line, "
            "Cmd+Shift+Enter: eval file.");

    // Global key bindings (ui.bindKey): a message-thread poll firing bound
    // Buttons/Toggles when no text field owns focus.
    if (appCtx_.uiState) {
        keyDispatch_ = std::make_unique<KeyDispatch>(*appCtx_.uiState,
                                                     dispatcher_);
        keyDispatch_->start();
    }

    // Print-drain coordinator: routes VM prints (any thread) to the in-flight
    // notebook cell or the console, and pumps the notebook's Run-All queue.
    startTimerHz(15);
}

MainComponent::~MainComponent() {
    stopTimer();
    // Stop the completion callback racing teardown, then restore stdout.
    guiState_.asyncEval.onFinished = nullptr;
    if (appCtx_.nrtvm)
        appCtx_.nrtvm->vm.setPrintOutput(stdout);
    saveSplitRatio();
}

void MainComponent::resized() {
    if (performView_) {
        performView_->setBounds(getLocalBounds());
        return;
    }
    juce::Component* center = notebookVisible_
        ? static_cast<juce::Component*>(notebook_.get())
        : static_cast<juce::Component*>(&editorPane_);
    juce::Component* comps[] = { center, resizer_.get(), &console_ };
    layout_.layOutComponents(comps, 3, 0, 0, getWidth(), getHeight(),
                             /*vertically=*/false, /*resizeOther=*/true);
    saveSplitRatio();
}

void MainComponent::togglePerform() {
    if (performView_) {
        performView_.reset();
        resized();
        return;
    }
    if (!appCtx_.uiState) return;
    performView_ = std::make_unique<PerformView>(
        appCtx_, dispatcher_,
        [this] { return notebook_->claimedPanels(); },
        [this] { togglePerform(); });
    addAndMakeVisible(*performView_);
    performView_->setBounds(getLocalBounds());
    performView_->refreshPanels();
}

void MainComponent::saveSplitRatio() {
    if (getWidth() <= 0) return;
    auto* center = notebookVisible_
        ? static_cast<juce::Component*>(notebook_.get())
        : static_cast<juce::Component*>(&editorPane_);
    double ratio = (double)center->getWidth() / getWidth();
    if (ratio > 0.05 && ratio < 0.95)
        settings_.setValue("splitRatio", ratio);
}

void MainComponent::showNotebook(bool show) {
    if (notebookVisible_ == show) return;
    notebookVisible_ = show;
    notebook_->setVisible(show);
    editorPane_.setVisible(!show);
    resized();
    commands_.commandStatusChanged();
}

// Route VM prints and result summaries to the console; drain from a timer
// since prints can arrive from any thread (scheduler, actors, engine).
void MainComponent::timerCallback() {
    std::uint64_t cell = guiState_.asyncEval.cellId;
    auto lines = guiState_.printCapture.drainLines();
    for (auto const& text : lines) {
        OutputLine line { text, LineKind::Output };
        if (cell != 0 && notebook_) notebook_->addCellOutput(cell, line);
        else console_.appendLine(line);
    }
    for (auto const& line : guiState_.output.drain())
        console_.appendLine(line);
    if (notebook_) notebook_->pumpRunQueue();
    refreshControlsWindows();
}

// A floating window per ui panel not claimed by the shown notebook. Panels
// the notebook renders inline (its panel cells) are skipped here.
void MainComponent::refreshControlsWindows() {
    auto* ui = appCtx_.uiState;
    if (!ui) return;

    std::vector<std::string> claimed;
    if (notebookVisible_ && notebook_) claimed = notebook_->claimedPanels();

    // Distinct root panels present in the registry, minus claimed ones.
    std::vector<std::string> wanted;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        for (auto& w : ui->widgets) {
            bool skip = false;
            for (auto const& root : claimed)
                if (bridge::panelUnderRoot(w->panel, root)) { skip = true; break; }
            if (skip) continue;
            // Reduce sub-panels ("root/x") to their root for one window.
            std::string root = w->panel;
            auto slash = root.find('/');
            if (slash != std::string::npos) root = root.substr(0, slash);
            if (std::find(wanted.begin(), wanted.end(), root) == wanted.end())
                wanted.push_back(root);
        }
    }

    // Close windows whose panel is gone or now claimed.
    for (auto it = controlsWindows_.begin(); it != controlsWindows_.end();) {
        if (std::find(wanted.begin(), wanted.end(), it->first) == wanted.end())
            it = controlsWindows_.erase(it);
        else ++it;
    }
    // Open windows for new panels.
    for (auto const& panel : wanted) {
        if (controlsWindows_.count(panel)) continue;
        auto win = std::make_unique<ControlsWindow>(*ui, panel, dispatcher_);
        win->onClose = [this, panel] {
            dispatcher_.queuePanelRemoval({ panel });
            controlsWindows_.erase(panel);
        };
        controlsWindows_[panel] = std::move(win);
    }
}

void MainComponent::logLine(String const& line) {
    console_.appendLine({ line.toStdString(), LineKind::Info });
}

// ---------------------------------------------------------------------------
// Unsaved-changes / file dialog flows (all async)
// ---------------------------------------------------------------------------

void MainComponent::confirmUnsavedChangesThen(std::function<void()> proceed) {
    if (!editorPane_.hasUnsavedChanges()) {
        proceed();
        return;
    }

    auto names = editorPane_.unsavedFileNames();
    String msg;
    if (names.size() == 1) {
        msg = "Do you want to save changes to \"" + names[0] + "\"?\n"
              "Your changes will be lost if you don't save them.";
    } else {
        msg << "You have " << (int)names.size()
            << " files with unsaved changes.\n"
               "Your changes will be lost if you don't save them.\n";
        for (auto const& n : names) msg << "\n  \xe2\x80\xa2 " << n;
    }

    juce::NativeMessageBox::showYesNoCancelBox(
        juce::MessageBoxIconType::WarningIcon, "Unsaved Changes", msg, this,
        juce::ModalCallbackFunction::create([this, proceed](int result) {
            // 1 = Save All, 2 = Don't Save, 0 = Cancel
            if (result == 1) { editorPane_.saveAll(); proceed(); }
            else if (result == 2) proceed();
        }));
}

void MainComponent::openFileFlow() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Open", juce::File(), "*.x;*.tzd");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this](juce::FileChooser const& fc) {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (file.hasFileExtension("tzd"))
                openNotebookFile(file);
            else if (!editorPane_.openFile(file))
                logLine("could not open " + file.getFullPathName());
        });
}

// ---------------------------------------------------------------------------
// Notebook file flows
// ---------------------------------------------------------------------------

void MainComponent::openNotebookFile(juce::File const& file) {
    String err;
    if (!notebook_->openFile(file, err)) {
        logLine("notebook open failed: " + err);
        return;
    }
    showNotebook(true);
    logLine("opened " + file.getFullPathName());
}

// Save the notebook; asks for a path if it has none (or if forceDialog).
void MainComponent::saveNotebookFlow(bool forceDialog) {
    juce::File existing = notebook_->currentFile();
    if (!forceDialog && existing != juce::File()) {
        String err;
        if (notebook_->saveToFile(existing, err))
            logLine("saved " + existing.getFullPathName());
        else
            logLine("notebook save failed: " + err);
        return;
    }
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save Notebook As",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("untitled.tzd"),
        "*.tzd");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](juce::FileChooser const& fc) {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            String err;
            if (notebook_->saveToFile(file, err))
                logLine("saved " + file.getFullPathName());
            else
                logLine("notebook save failed: " + err);
        });
}

// Save the active tab; asks for a path if it has none (or if forceDialog).
// `done(true)` fires only after a successful save.
void MainComponent::saveActiveFlow(bool forceDialog,
                                   std::function<void(bool)> done) {
    if (!forceDialog && editorPane_.activeHasFilePath()) {
        bool ok = editorPane_.saveActive();
        if (done) done(ok);
        return;
    }
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save As",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile(editorPane_.activeTabName()),
        "*.x");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, done](juce::FileChooser const& fc) {
            auto file = fc.getResult();
            bool ok = file != juce::File() && editorPane_.saveActiveAs(file);
            if (done) done(ok);
        });
}

void MainComponent::closeActiveTabFlow() {
    int idx = editorPane_.activeTabIndex();
    if (!editorPane_.tabModified(idx)) {
        editorPane_.closeTab(idx);
        return;
    }
    juce::NativeMessageBox::showYesNoCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Unsaved Changes",
        "Do you want to save changes to \"" + editorPane_.tabName(idx) + "\"?\n"
        "Your changes will be lost if you don't save them.",
        this,
        juce::ModalCallbackFunction::create([this](int result) {
            // 1 = Save, 2 = Don't Save, 0 = Cancel
            if (result == 1) {
                saveActiveFlow(false, [this](bool ok) {
                    if (ok) editorPane_.closeTab(editorPane_.activeTabIndex());
                });
            } else if (result == 2) {
                editorPane_.closeTab(editorPane_.activeTabIndex());
            }
        }));
}

// ---------------------------------------------------------------------------
// Eval
// ---------------------------------------------------------------------------

void MainComponent::launchEval(String const& code, int flashStart, int flashEnd) {
    if (!session_ || guiState_.asyncEval.busy() || code.trim().isEmpty())
        return;
    editorPane_.clearErrorMarkers();
    guiState_.asyncEval.launch(code.toStdString(), appCtx_, *session_,
                               flashStart, flashEnd);
}

void MainComponent::collectEvalResult() {
    auto& ae = guiState_.asyncEval;
    if (ae.busy() || !ae.finished()) return;

    // Notebook cell eval: join, route result/errors/markers to the cell.
    if (ae.cellId != 0) {
        ae.join();
        std::uint64_t cellId = ae.cellId;
        auto result = ae.result;   // copy before cellId is cleared
        std::string code = ae.code;
        ae.cellId = 0;
        // Drain any trailing cell prints before the result summary lands.
        timerCallback();
        if (notebook_) notebook_->onCellEvalDone(cellId, result, code);
        return;
    }

    // Editor eval: collect() formats result/errors into guiState_.output
    // (drained to the console by the timer) and we forward flash/markers.
    if (!ae.collect(guiState_)) return;
    if (!ae.result.errors.empty()) {
        std::vector<std::pair<int, String>> markers;
        for (auto const& e : ae.result.errors) {
            // SourceLoc lines are 1-based within the evaluated text.
            int line = ae.flashStart + (int)e.loc.start.line - 1;
            markers.emplace_back(line, String(e.message));
        }
        editorPane_.setErrorMarkersFromEval(markers);
    } else if (ae.flashStart >= 0 && ae.flashEnd >= 0) {
        editorPane_.triggerFlash(ae.flashStart, ae.flashEnd);
    }
    timerCallback(); // flush output buffer to the console now
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& ids) {
    ids.addArray({
        cmd::fileNew, cmd::fileNewNotebook, cmd::fileOpen, cmd::fileSave,
        cmd::fileSaveAs, cmd::fileSaveCopy, cmd::fileClose,
#if !JUCE_MAC
        cmd::quit,
#endif
        cmd::editUndo, cmd::editRedo, cmd::editCut, cmd::editCopy,
        cmd::editPaste, cmd::editSelectAll, cmd::editClearOutput,
        cmd::editToggleComment, cmd::editIndent, cmd::editOutdent,
        cmd::findShow, cmd::findNext, cmd::findPrevious,
        cmd::findUseSelection, cmd::findUseSelectionReplace,
        cmd::fontIncrease, cmd::fontDecrease, cmd::toggleNotebookView,
        cmd::togglePerform,
        cmd::evalSelection, cmd::evalLine, cmd::evalFile,
    });
    for (int i = 0; i < cmd::kNumEditorFontSizes; ++i)
        ids.add(cmd::fontSetBase + i);
    for (int i = 0; i < themeCount; ++i)
        ids.add(cmd::themeSetBase + i);
}

void MainComponent::getCommandInfo(juce::CommandID id,
                                   juce::ApplicationCommandInfo& info) {
    using juce::KeyPress;
    using juce::ModifierKeys;
    auto mod = ModifierKeys::commandModifier;
    auto modShift = ModifierKeys::commandModifier | ModifierKeys::shiftModifier;

    auto set = [&](String const& name, String const& category) {
        info.setInfo(name, name, category, 0);
    };

    // Font-size and theme ranges
    if (id >= cmd::fontSetBase && id < cmd::fontSetBase + cmd::kNumEditorFontSizes) {
        int i = id - cmd::fontSetBase;
        set(String((int)cmd::kEditorFontSizes[i]) + " pt", "View");
        info.setTicked(fontIndex_ == i);
        return;
    }
    if (id >= cmd::themeSetBase && id < cmd::themeSetBase + themeCount) {
        int i = id - cmd::themeSetBase;
        set(kAppThemeNames[i], "View");
        info.setTicked(currentTheme() == i);
        return;
    }

    switch (id) {
    case cmd::fileNew:
        set("New", "File");
        info.addDefaultKeypress('n', mod);
        break;
    case cmd::fileNewNotebook:
        set("New Notebook", "File");
        info.addDefaultKeypress('n', modShift);
        break;
    case cmd::fileOpen:
        set("Open...", "File");
        info.addDefaultKeypress('o', mod);
        break;
    case cmd::fileSave:
        set("Save", "File");
        info.addDefaultKeypress('s', mod);
        break;
    case cmd::fileSaveAs:
        set("Save As...", "File");
        info.addDefaultKeypress('s', modShift);
        break;
    case cmd::fileSaveCopy:
        set("Save a Copy As...", "File");
        break;
    case cmd::fileClose:
        set("Close Tab", "File");
        info.addDefaultKeypress('w', mod);
        break;
    case cmd::quit:
        set("Quit", "File");
        info.addDefaultKeypress('q', mod);
        break;

    case cmd::editUndo:
        set("Undo", "Edit");
        info.addDefaultKeypress('z', mod);
        break;
    case cmd::editRedo:
        set("Redo", "Edit");
        info.addDefaultKeypress('z', modShift);
        break;
    case cmd::editCut:
        set("Cut", "Edit");
        info.addDefaultKeypress('x', mod);
        break;
    case cmd::editCopy:
        set("Copy", "Edit");
        info.addDefaultKeypress('c', mod);
        break;
    case cmd::editPaste:
        set("Paste", "Edit");
        info.addDefaultKeypress('v', mod);
        break;
    case cmd::editSelectAll:
        set("Select All", "Edit");
        info.addDefaultKeypress('a', mod);
        break;
    case cmd::editClearOutput:
        set("Clear Output", "Edit");
        info.addDefaultKeypress('k', mod);
        break;
    case cmd::editToggleComment:
        set("Toggle Line Comment", "Edit");
        info.addDefaultKeypress('/', mod);
        break;
    case cmd::editIndent:
        set("Indent", "Edit");
        info.addDefaultKeypress(']', mod);
        break;
    case cmd::editOutdent:
        set("Outdent", "Edit");
        info.addDefaultKeypress('[', mod);
        break;

    case cmd::findShow:
        set("Find...", "Find");
        info.addDefaultKeypress('f', mod);
        break;
    case cmd::findNext:
        set("Find Next", "Find");
        info.addDefaultKeypress('g', mod);
        break;
    case cmd::findPrevious:
        set("Find Previous", "Find");
        info.addDefaultKeypress('g', modShift);
        break;
    case cmd::findUseSelection:
        set("Use Selection for Find", "Find");
        info.addDefaultKeypress('e', mod);
        break;
    case cmd::findUseSelectionReplace:
        set("Use Selection for Replace", "Find");
        info.addDefaultKeypress('e', modShift);
        break;

    case cmd::fontIncrease:
        set("Increase Font Size", "View");
        info.addDefaultKeypress('=', mod);
        break;
    case cmd::fontDecrease:
        set("Decrease Font Size", "View");
        info.addDefaultKeypress('-', mod);
        break;
    case cmd::toggleNotebookView:
        set("Toggle Notebook / Editor", "View");
        info.addDefaultKeypress('\\', mod);
        break;
    case cmd::togglePerform:
        set("Perform Mode", "View");
        info.addDefaultKeypress('p', modShift);
        info.setTicked(performView_ != nullptr);
        break;

    case cmd::evalSelection:
        set("Evaluate Selection", "Eval");
        info.addDefaultKeypress(KeyPress::returnKey, mod);
        break;
    case cmd::evalLine:
        set("Evaluate Line", "Eval");
        info.addDefaultKeypress(KeyPress::returnKey,
                                ModifierKeys::shiftModifier);
        break;
    case cmd::evalFile:
        set("Evaluate File", "Eval");
        info.addDefaultKeypress(KeyPress::returnKey, modShift);
        break;

    default:
        set("Unknown", "Misc");
        break;
    }
}

bool MainComponent::perform(InvocationInfo const& info) {
    auto id = info.commandID;

    if (id >= cmd::fontSetBase && id < cmd::fontSetBase + cmd::kNumEditorFontSizes) {
        applyFontIndex(id - cmd::fontSetBase);
        return true;
    }
    if (id >= cmd::themeSetBase && id < cmd::themeSetBase + themeCount) {
        applyTheme(id - cmd::themeSetBase);
        return true;
    }

    auto todo = [&](char const* name) {
        logLine(String("[command] ") + name);
        return true;
    };

    switch (id) {
    // -- File ---------------------------------------------------------------
    case cmd::fileNew:
        editorPane_.newTab();
        showNotebook(false);
        return true;
    case cmd::fileNewNotebook:
        notebook_->newDocument();
        showNotebook(true);
        return true;
    case cmd::fileOpen:
        openFileFlow();
        return true;
    case cmd::fileSave:
        if (notebookVisible_) saveNotebookFlow(false);
        else saveActiveFlow(false);
        return true;
    case cmd::fileSaveAs:
        if (notebookVisible_) saveNotebookFlow(true);
        else saveActiveFlow(true);
        return true;
    case cmd::fileSaveCopy: {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Save a Copy As",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile(editorPane_.activeTabName()),
            "*.x");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](juce::FileChooser const& fc) {
                auto file = fc.getResult();
                if (file != juce::File()) editorPane_.saveCopy(file);
            });
        return true;
    }
    case cmd::fileClose:
        closeActiveTabFlow();
        return true;
    case cmd::quit:
        confirmUnsavedChangesThen([] { juce::JUCEApplicationBase::quit(); });
        return true;

    // -- Edit -----------------------------------------------------------
    // Menu edit ops act on the code editor. When another text field has
    // keyboard focus its native key handling covers the shortcuts; per-
    // focus menu routing (console copy, find bar) lands later.
    // In the notebook, undo/redo drive the document history tree; the
    // per-cell editors keep their own native character-level undo.
    case cmd::editUndo:
        if (notebookVisible_) notebook_->undoDocument();
        else editorPane_.undo();
        return true;
    case cmd::editRedo:
        if (notebookVisible_) notebook_->redoDocument();
        else editorPane_.redo();
        return true;
    case cmd::editCut:    editorPane_.cutToClipboard(); return true;
    case cmd::editCopy:   editorPane_.copyToClipboard(); return true;
    case cmd::editPaste:  editorPane_.pasteFromClipboard(); return true;
    case cmd::editSelectAll: editorPane_.selectAll(); return true;
    case cmd::editClearOutput:
        console_.clear();
        return true;
    case cmd::editToggleComment: editorPane_.toggleComment(); return true;
    case cmd::editIndent:        editorPane_.indentSelection(); return true;
    case cmd::editOutdent:       editorPane_.outdentSelection(); return true;

    // -- Find -------------------------------------------------------------
    case cmd::findShow:
        editorPane_.showFind(editorPane_.getSelectedText());
        return true;
    case cmd::findNext:
        editorPane_.findNext();
        return true;
    case cmd::findPrevious:
        editorPane_.findPrevious();
        return true;
    case cmd::findUseSelection: {
        String sel = editorPane_.getSelectedText();
        if (sel.isNotEmpty()) editorPane_.showFind(sel);
        return true;
    }
    case cmd::findUseSelectionReplace: {
        String sel = editorPane_.getSelectedText();
        if (sel.isNotEmpty()) editorPane_.seedReplace(sel);
        return true;
    }

    // -- View -------------------------------------------------------------
    case cmd::fontIncrease:
        applyFontIndex(fontIndex_ + 1);
        return true;
    case cmd::fontDecrease:
        applyFontIndex(fontIndex_ - 1);
        return true;
    case cmd::toggleNotebookView:
        showNotebook(!notebookVisible_);
        return true;
    case cmd::togglePerform:
        togglePerform();
        return true;

    // -- Eval ---------------------------------------------------------------
    // With the notebook shown, Cmd+Enter / Shift+Enter run the focused cell
    // and Cmd+Shift+Enter runs every cell, matching the ImGui app.
    case cmd::evalSelection: {
        if (notebookVisible_) { notebook_->runFocusedCell(); return true; }
        String code = editorPane_.getSelectedText();
        int startLine, endLine;
        if (code.isEmpty()) {
            code = editorPane_.getCurrentBlockText(startLine, endLine);
        } else {
            startLine = endLine = editorPane_.cursorLine();
        }
        launchEval(code, startLine, endLine);
        return true;
    }
    case cmd::evalLine:
        if (notebookVisible_) { notebook_->runFocusedCell(); return true; }
        {
            int line = editorPane_.cursorLine();
            launchEval(editorPane_.getCurrentLineText(), line, line);
        }
        return true;
    case cmd::evalFile:
        if (notebookVisible_) { notebook_->runAll(); return true; }
        launchEval(editorPane_.getAllText(), 0, editorPane_.cursorLine());
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Self-test hooks
// ---------------------------------------------------------------------------

void MainComponent::testTypeIntoEditor(String const& text) {
    if (auto* ed = editorPane_.activeEditor())
        ed->insertTextAtCaret(text);
}

bool MainComponent::testEvalCollected() const {
    auto& ae = guiState_.asyncEval;
    return !ae.busy() && !ae.threadActive_;
}

int MainComponent::testWidgetCount() const {
    auto* ui = appCtx_.uiState;
    if (!ui) return 0;
    std::lock_guard<std::mutex> lock(ui->mtx);
    return (int)ui->widgets.size();
}

double MainComponent::testDriveFirstSlider() {
    auto* ui = appCtx_.uiState;
    if (!ui) return -1.0;
    std::lock_guard<std::mutex> lock(ui->mtx);
    for (auto& w : ui->widgets) {
        if (w->kind == bridge::UIWidgetKind::Slider) {
            if (w->values.empty()) w->values.resize(1);
            w->values[0] = w->spec.map(0.75);
            w->dirtyEngine = true;
            return w->values[0];
        }
    }
    return -1.0;
}

void MainComponent::testShowDemo(String const& which) {
    if (which == "find") {
        editorPane_.showFind("blip");
    } else if (which == "flash") {
        editorPane_.triggerFlash(6, 8);
        editorPane_.setErrorMarkersFromEval({ { 12, "example error marker" } });
    } else if (which == "history") {
        showNotebook(true);
        notebook_->toggleHistoryWindow();
    } else if (which == "perform") {
        showNotebook(true);
        togglePerform();
    }
}

String MainComponent::testLastEvalSummary() const {
    auto& r = guiState_.asyncEval.result;
    if (!r.errors.empty()) return "errors:" + String((int)r.errors.size());
    if (r.hasValue) return String(r.formattedValue) + " : " + String(r.typeName);
    return "(no value)";
}

void MainComponent::applyTheme(int themeIdx) {
    themeIdx = juce::jlimit(0, themeCount - 1, themeIdx);
    lookAndFeel_.applyTheme(themeIdx);
    settings_.setValue("theme", themeIdx);
    if (auto* top = getTopLevelComponent()) {
        top->sendLookAndFeelChange();
        top->repaint();
    }
    commands_.commandStatusChanged(); // refresh menu check marks
}

void MainComponent::applyFontIndex(int idx) {
    fontIndex_ = juce::jlimit(0, cmd::kNumEditorFontSizes - 1, idx);
    settings_.setValue("fontIndex", fontIndex_);
    float px = cmd::kEditorFontSizes[fontIndex_];
    editorPane_.setFontSize(px);
    console_.setFontSize(px);
    if (notebook_) notebook_->setFontSize(px);  // cells + relayout
    commands_.commandStatusChanged();
}

}
