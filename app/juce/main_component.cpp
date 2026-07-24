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
#include "BinaryData.h"
#include "graph_edits.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_ui_node_controls.hpp"
#include "nrt_vm.hpp"
#include "repl_session.hpp"
#include "module_compiler.hpp"
#include "project_paths.hpp"
#include <cstdio>
#include <cstdlib>

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
        appCtx_, guiState_,
        [this] {
            // Re-anchor document-relative imports every time the notebook
            // fetches the session (i.e. before each cell eval).
            updateSessionDocumentPath();
            return session_.get();
        },
        dispatcher_);
    addChildComponent(*notebook_);

    // Finished widget gestures (slider drags, toggle flips, key-bound
    // widgets) become one history commit when they touch claimed panels.
    dispatcher_.onGesturesEnded =
        [this](std::vector<std::pair<std::string, std::string>> const& w) {
            if (notebook_) notebook_->onWidgetGesturesEnded(w);
        };

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
    juce::Component* center =
          centerMode_ == CenterMode::notebook
        ? static_cast<juce::Component*>(notebook_.get())
        : centerMode_ == CenterMode::graph
        ? static_cast<juce::Component*>(graphView_.get())
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

// Materialize a live node's control interface (from its def's
// ControlSpecs) as engine-bound widgets in a panel named after the node;
// the floating controls window for it appears via refreshControlsWindows.
// Values are write-only for now: widgets start at each spec's init (or
// keep their value if the panel is already open) -- there is no engine
// readback of current control values yet.
//
// Defs with buffers also get a "load <buffer>" button per buffer slot
// (file dialog -> replaceBuffer) and, once a file is loaded, a waveform
// overview row above it.
void MainComponent::openNodeControls(long long nodeID,
                                     std::string const& defName, int silo) {
    if (!appCtx_.uiState || !appCtx_.engine) return;
    std::string panel = defName + " #" + std::to_string(nodeID);
    int bound = bridge::materializeNodeControls(appCtx_.uiState, appCtx_.engine,
                                                panel, nodeID, silo,
                                                defName.c_str());
    if (bound < 0) {
        console_.appendLine({"graph: unknown synthdef \"" + defName + "\"",
                             LineKind::Error});
        return;
    }

    engine::DefDesc def;
    if (engine::getDefDesc(appCtx_.engine, defName.c_str(), def)) {
        for (auto const& b : def.buffers) {
            // Waveform row for an already-loaded file (path recorded by
            // audio_engine.loadBuffer or a previous Load here).
            std::string path;
            {
                std::lock_guard<std::mutex> lock(appCtx_.bufferPathsMtx);
                auto it = appCtx_.bufferPaths.find({nodeID, b.bufID});
                if (it != appCtx_.bufferPaths.end()) path = it->second;
            }
            if (!path.empty())
                bridge::bindWaveformWidget(appCtx_.uiState, panel, b.name,
                                           path.c_str());

            auto* ui = appCtx_.uiState;
            std::lock_guard<std::mutex> lock(ui->mtx);
            bridge::UIWidget* w = ui->upsert(panel, "load " + b.name,
                                             bridge::UIWidgetKind::Button,
                                             bridge::UISpec{}, {});
            w->hostAction = [safe = juce::Component::SafePointer<MainComponent>(this),
                             nodeID, silo, panel,
                             bufID = b.bufID, bufName = b.name] {
                if (safe == nullptr) return;
                safe->loadBufferFlow(nodeID, silo, panel, bufID, bufName);
            };
            ++bound;
        }
    }

    if (bound == 0) {
        console_.appendLine({"graph: " + panel + " has no controls",
                             LineKind::Info});
        return;
    }
    dispatcher_.ensureRunning();   // push the fresh bindings to the engine
    refreshControlsWindows();      // float the window now, not next tick
    if (auto it = controlsWindows_.find(panel); it != controlsWindows_.end())
        it->second->toFront(true);
}

// File dialog -> load the chosen audio file into a node's buffer slot and
// show/refresh its waveform row in the node's control panel.
void MainComponent::loadBufferFlow(long long nodeID, int silo,
                                   std::string const& panel,
                                   long long bufID, std::string const& bufName) {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load audio file into \"" + bufName + "\"", dialogDefaultDir(),
        "*.wav;*.aif;*.aiff;*.caf;*.mp3;*.m4a;*.flac;*.ogg");
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this, nodeID, silo, panel, bufID, bufName, chooser]
        (juce::FileChooser const& fc) {
            auto file = fc.getResult();
            if (file == juce::File{}) return; // cancelled
            rememberDialogDir(file);
            std::string path = file.getFullPathName().toStdString();

            long long frames = 0;
            int err = graph::loadBufferFile(appCtx_.engine, silo, nodeID,
                                            bufID, path, &frames);
            if (err != 0) {
                console_.appendLine({"graph: load \"" + path + "\" failed: "
                                         + graph::errText(err),
                                     LineKind::Error});
                return;
            }
            {
                std::lock_guard<std::mutex> lock(appCtx_.bufferPathsMtx);
                appCtx_.bufferPaths[{nodeID, bufID}] = path;
            }
            bridge::bindWaveformWidget(appCtx_.uiState, panel, bufName,
                                       path.c_str());
            console_.appendLine({"graph: loaded \""
                                     + file.getFileName().toStdString()
                                     + "\" (" + std::to_string(frames)
                                     + " frames) into " + panel + " \""
                                     + bufName + "\"",
                                 LineKind::Info});
        });
}

void MainComponent::togglePluginBrowser() {
    if (!pluginBrowser_) {
        pluginBrowser_ = std::make_unique<PluginBrowserWindow>(appCtx_);
        pluginBrowser_->onClose = [this] { commands_.commandStatusChanged(); };
    } else if (pluginBrowser_->isVisible()) {
        pluginBrowser_->setVisible(false);
    } else {
        pluginBrowser_->setVisible(true);
        pluginBrowser_->toFront(true);
        pluginBrowser_->refresh();
    }
    commands_.commandStatusChanged();
}

void MainComponent::saveSplitRatio() {
    if (getWidth() <= 0) return;
    auto* center =
          centerMode_ == CenterMode::notebook
        ? static_cast<juce::Component*>(notebook_.get())
        : centerMode_ == CenterMode::graph
        ? static_cast<juce::Component*>(graphView_.get())
        : static_cast<juce::Component*>(&editorPane_);
    double ratio = (double)center->getWidth() / getWidth();
    if (ratio > 0.05 && ratio < 0.95)
        settings_.setValue("splitRatio", ratio);
}

void MainComponent::showNotebook(bool show) {
    setCenterMode(show ? CenterMode::notebook : CenterMode::editor);
}

void MainComponent::setCenterMode(CenterMode m) {
    if (centerMode_ == m) return;
    if (centerMode_ != CenterMode::graph)
        lastDocMode_ = centerMode_; // where to return when the graph closes
    centerMode_ = m;
    if (m == CenterMode::graph && !graphView_) {
        graphView_ = std::make_unique<GraphView>(appCtx_);
        graphView_->onLog = [this](std::string const& msg, bool isError) {
            console_.appendLine({msg, isError ? LineKind::Error : LineKind::Info});
        };
        graphView_->onOpenNodeControls =
            [this](long long nodeID, std::string const& defName, int silo) {
                openNodeControls(nodeID, defName, silo);
            };
        addChildComponent(*graphView_);
    }
    notebook_->setVisible(m == CenterMode::notebook);
    editorPane_.setVisible(m == CenterMode::editor);
    if (graphView_) graphView_->setVisible(m == CenterMode::graph);
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

    // Poll the disk for files edited outside the app about once a second
    // (the timer runs at 15 Hz). A flipped flag marks the tab and may change
    // menu state, so refresh the command manager when something changed.
    if (++externalCheckTicks_ >= 15) {
        externalCheckTicks_ = 0;
        if (editorPane_.checkExternalChanges())
            commands_.commandStatusChanged();
    }

    // Mirror unsaved work into the close box (macOS documentEdited dot),
    // matching the quit prompt: a dirty notebook or editor tab counts.
    bool edited = editorPane_.hasUnsavedChanges()
               || (notebook_ && notebook_->isModified());
    if (edited != documentEditedShown_) {
        documentEditedShown_ = edited;
        if (auto* peer = getPeer())
            peer->setDocumentEditedStatus(edited);
    }
}

// A floating window per ui panel not claimed by the open notebook. Panels
// claimed by the notebook NEVER float -- not while the notebook is hidden
// either (closing a floating window deletes its widgets, a trap when they
// were only borrowed from the document). Hidden notebook = its controls
// are simply not shown until it returns.
void MainComponent::refreshControlsWindows() {
    auto* ui = appCtx_.uiState;
    if (!ui) return;

    std::vector<std::string> claimed;
    if (notebook_) claimed = notebook_->claimedPanels();

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
    // Both the editor tabs AND the notebook document hold unsaved state; a
    // dirty notebook alone must still prompt.
    auto names = editorPane_.unsavedFileNames();
    bool notebookDirty = notebook_ && notebook_->isModified();
    if (std::getenv("TZPL_JUCE_DEMO") != nullptr)
        std::fprintf(stderr,
                     "confirmUnsavedChangesThen: tabs=%d notebookDirty=%d\n",
                     (int)names.size(), notebookDirty ? 1 : 0);
    if (names.empty() && !notebookDirty) {
        proceed();
        return;
    }
    if (notebookDirty) {
        juce::File f = notebook_->currentFile();
        names.insert(names.begin(), f != juce::File() ? f.getFileName()
                                                      : String("untitled notebook"));
    }

    String msg;
    if (names.size() == 1) {
        msg = "Do you want to save changes to \"" + names[0] + "\"?\n"
              "Your changes will be lost if you don't save them.";
    } else {
        msg << "You have " << (int)names.size()
            << " documents with unsaved changes.\n"
               "Your changes will be lost if you don't save them.\n";
        for (auto const& n : names) msg << "\n  \xe2\x80\xa2 " << n;
    }

    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Unsaved Changes")
        .withMessage(msg)
        .withButton(names.size() > 1 ? "Save All" : "Save")
        .withButton("Don't Save")
        .withButton("Cancel")
        .withAssociatedComponent(this);

    // showAsync reports the BUTTON INDEX: 0 = Save(All), 1 = Don't Save,
    // 2 = Cancel. Saving an untitled document opens a file chooser, so
    // `proceed` must wait for those to finish -- and must not run if the
    // user backs out of any of them, or their contents would be lost.
    juce::NativeMessageBox::showAsync(
        options, [this, proceed, notebookDirty](int result) {
            if (result == 0) {
                saveAllTabsThen([this, proceed, notebookDirty](bool ok) {
                    if (!ok) return;   // a chooser was cancelled: stay open
                    if (notebookDirty)
                        saveNotebookFlow(false, [proceed](bool saved) {
                            if (saved) proceed();
                        });
                    else
                        proceed();
                });
            } else if (result == 1) {
                proceed();
            }
        });
}

void MainComponent::openFileFlow() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Open", dialogDefaultDir(), "*.x;*.tzd");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this](juce::FileChooser const& fc) {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            rememberDialogDir(file);
            openPath(file);
        });
}

void MainComponent::openPath(juce::File const& file) {
    if (!file.existsAsFile()) {
        logLine("no such file: " + file.getFullPathName());
        return;
    }
    // Distribution examples are templates: open an untitled copy so a user
    // edit is never saved into the (possibly read-only) distribution folder
    // and never clobbered by an update.
    if (isDistExample(file)) {
        if (file.hasFileExtension("tzd")) {
            confirmNotebookDiscardThen([this, file] {
                // Detach only on success: a failed open keeps the previous
                // notebook, whose file association must survive.
                if (!openNotebookFile(file)) return;
                notebook_->detachFile();
                logLine("(example: editing a copy; Save asks for a location)");
            });
        } else if (editorPane_.openFileAsCopy(file)) {
            logLine("opened a copy of " + file.getFullPathName());
        } else {
            logLine("could not open " + file.getFullPathName());
        }
        return;
    }
    registerProjectFor(file);
    if (file.hasFileExtension("tzd"))
        // Opening a notebook replaces the current one.
        confirmNotebookDiscardThen(
            [this, file] { openNotebookFile(file); });
    else if (!editorPane_.openFile(file))
        logLine("could not open " + file.getFullPathName());
}

// The engine config of a project discovered mid-session can't be applied (the
// engine is already running); its modules/ can, so imports resolve. Engine
// settings apply when the app is next launched on a file in the project.
void MainComponent::registerProjectFor(juce::File const& file) {
    std::string root = tzplapp::findProjectRoot(
        file.getFullPathName().toStdString());
    if (root.empty()) return;
    noteRecentProject(String(root), file.getFullPathName());
    if (!appCtx_.moduleCompiler) return;
    juce::File modulesDir = juce::File(String(root)).getChildFile("modules");
    if (!modulesDir.isDirectory()) return;
    std::string dir = modulesDir.getFullPathName().toStdString();
    if (appCtx_.moduleCompiler->addIncludePath(dir))
        logLine("project modules: " + modulesDir.getFullPathName());
    // Silo module compilers snapshot the app's paths when the silo VM is
    // attached; keep already-attached silos in sync so a project module
    // imports the same on a silo as in the notebook.
    for (auto& silo : appCtx_.siloVMs)
        if (silo.moduleCompiler) silo.moduleCompiler->addIncludePath(dir);
}

// Recent projects persist in the settings file as most-recent-first
// "root|lastDocument" lines; the File menu submenu shows the roots and
// opening one reopens its last document.
void MainComponent::noteRecentProject(String const& root, String const& doc) {
    auto lines = juce::StringArray::fromLines(
        settings_.getValue("recentProjects"));
    lines.removeEmptyStrings();
    for (int i = lines.size(); --i >= 0;)
        if (lines[i].upToFirstOccurrenceOf("|", false, false) == root)
            lines.remove(i);
    lines.insert(0, root + "|" + doc);
    while (lines.size() > cmd::kMaxRecentProjects)
        lines.remove(lines.size() - 1);
    settings_.setValue("recentProjects", lines.joinIntoString("\n"));
}

juce::StringArray MainComponent::recentProjectRoots() const {
    juce::StringArray roots;
    for (auto& l : juce::StringArray::fromLines(
             settings_.getValue("recentProjects")))
        if (l.isNotEmpty())
            roots.add(l.upToFirstOccurrenceOf("|", false, false));
    return roots;
}

void MainComponent::openRecentProject(int index) {
    auto lines = juce::StringArray::fromLines(
        settings_.getValue("recentProjects"));
    lines.removeEmptyStrings();
    if (index < 0 || index >= lines.size()) return;
    String root = lines[index].upToFirstOccurrenceOf("|", false, false);
    String doc = lines[index].fromFirstOccurrenceOf("|", false, false);
    if (juce::File docFile(doc); docFile.existsAsFile()) {
        openPath(docFile);
        return;
    }
    // The last document is gone; fall back to an Open dialog in the project.
    juce::File rootDir(root);
    if (!rootDir.isDirectory()) {
        logLine("project no longer exists: " + root);
        return;
    }
    appCtx_.projectDir = rootDir.getFullPathName().toStdString();
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Open", rootDir, "*.x;*.tzd");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this](juce::FileChooser const& fc) {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            rememberDialogDir(file);
            openPath(file);
        });
}

// ---------------------------------------------------------------------------
// Notebook file flows
// ---------------------------------------------------------------------------

void MainComponent::openNewNotebook() {
    notebook_->newDocument();
    showNotebook(true);
}

// ---------------------------------------------------------------------------
// About box: the splash artwork over black with version + license text
// beneath it. Click anywhere (or Escape / the close button) dismisses it.
// ---------------------------------------------------------------------------

namespace {

class AboutBox : public juce::Component {
public:
    AboutBox() {
        image_ = juce::ImageCache::getFromMemory(
            BinaryData::TzopilotlSplash_png,
            BinaryData::TzopilotlSplash_pngSize);
        setSize(440, 520);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);
        auto area = getLocalBounds();
        auto imageArea = area.removeFromTop(getWidth());
        g.drawImage(image_, imageArea.toFloat(),
                    juce::RectanglePlacement::centred);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        auto line = [&](juce::String const& text, int height) {
            g.drawText(text, area.removeFromTop(height),
                       juce::Justification::centred);
        };
        line("Tzopilotl " JUCE_APPLICATION_VERSION_STRING, 26);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(13.0f));
        line("Copyright (C) 2026 James McCartney", 20);
        line("Licensed under the GNU General Public License v3", 20);
    }

    void mouseUp(juce::MouseEvent const&) override {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    }

private:
    juce::Image image_;
};

} // namespace

void MainComponent::showAboutBox() {
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(new AboutBox());
    opts.dialogTitle = "About Tzopilotl";
    opts.dialogBackgroundColour = juce::Colours::black;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

bool MainComponent::openNotebookFile(juce::File const& file) {
    String err;
    if (!notebook_->openFile(file, err)) {
        logLine("notebook open failed: " + err);
        return false;
    }
    showNotebook(true);
    logLine("opened " + file.getFullPathName());
    return true;
}

// Replacing the open notebook (New Notebook / opening a .tzd) discards it.
// Ask first when it has unsaved changes; `proceed` runs only if the user
// saved or explicitly discarded.
void MainComponent::confirmNotebookDiscardThen(std::function<void()> proceed) {
    if (!notebook_ || !notebook_->isModified()) {
        proceed();
        return;
    }
    juce::File f = notebook_->currentFile();
    String name = f != juce::File() ? f.getFileName()
                                    : String("untitled notebook");
    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Unsaved Changes")
        .withMessage("Do you want to save changes to \"" + name + "\"?\n"
                     "Your changes will be lost if you don't save them.")
        .withButton("Save")
        .withButton("Don't Save")
        .withButton("Cancel")
        .withAssociatedComponent(this);

    // 0 = Save, 1 = Don't Save, 2 = Cancel. An untitled notebook's Save
    // opens a chooser, so `proceed` waits for the write to succeed.
    juce::NativeMessageBox::showAsync(options, [this, proceed](int result) {
        if (result == 0)
            saveNotebookFlow(false, [proceed](bool ok) { if (ok) proceed(); });
        else if (result == 1)
            proceed();
    });
}

void MainComponent::saveAllTabsThen(std::function<void(bool)> done) {
    editorPane_.saveAll();   // synchronous: every modified tab that has a path
    // saveAll() swallows write failures. A tab still modified despite having
    // a path means its write failed (permissions, deleted directory); report
    // it rather than letting the caller quit over it.
    for (int i = 0; i < editorPane_.tabCount(); ++i) {
        if (editorPane_.tabModified(i) && editorPane_.tabHasFilePath(i)) {
            logLine("could not save " + editorPane_.tabName(i));
            done(false);
            return;
        }
    }
    saveNextUntitledTabThen(std::move(done));
}

// One untitled tab per call, recursing through its own chooser callback.
void MainComponent::saveNextUntitledTabThen(std::function<void(bool)> done) {
    int idx = -1;
    for (int i = 0; i < editorPane_.tabCount(); ++i) {
        if (editorPane_.tabModified(i) && !editorPane_.tabHasFilePath(i)) {
            idx = i;
            break;
        }
    }
    if (idx < 0) { done(true); return; }

    String name = editorPane_.tabName(idx);
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save \"" + name + "\"",
        dialogDefaultDir().getChildFile(name),
        "*.x");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, idx, done](juce::FileChooser const& fc) {
            auto file = fc.getResult();
            if (file == juce::File() || !editorPane_.saveTabAs(idx, file)) {
                done(false);   // cancelled or write failed -- don't proceed
                return;
            }
            rememberDialogDir(file);
            // The tab must no longer qualify, or the recursion never ends.
            if (editorPane_.tabModified(idx)
                && !editorPane_.tabHasFilePath(idx)) {
                logLine("could not save " + file.getFullPathName());
                done(false);
                return;
            }
            // Defer: replacing fileChooser_ here would destroy the chooser
            // that owns this running lambda.
            juce::MessageManager::callAsync(
                [this, done] { saveNextUntitledTabThen(done); });
        });
}

// Save the notebook; asks for a path if it has none (or if forceDialog).
void MainComponent::saveNotebookFlow(bool forceDialog,
                                     std::function<void(bool)> done) {
    auto write = [this](juce::File const& file) {
        String err;
        bool ok = notebook_->saveToFile(file, err);
        logLine(ok ? "saved " + file.getFullPathName()
                   : "notebook save failed: " + err);
        return ok;
    };
    juce::File existing = notebook_->currentFile();
    if (!forceDialog && existing != juce::File()) {
        bool ok = write(existing);
        if (done) done(ok);
        return;
    }
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save Notebook As",
        dialogDefaultDir().getChildFile("untitled.tzd"),
        "*.tzd");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, write, done](juce::FileChooser const& fc) {
            auto file = fc.getResult();
            bool ok = file != juce::File() && write(file);
            if (ok) rememberDialogDir(file);
            if (done) done(ok);
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
        dialogDefaultDir().getChildFile(editorPane_.activeTabName()),
        "*.x");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, done](juce::FileChooser const& fc) {
            auto file = fc.getResult();
            bool ok = file != juce::File() && editorPane_.saveActiveAs(file);
            if (ok) rememberDialogDir(file);
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

void MainComponent::revertActiveFlow() {
    int idx = editorPane_.activeTabIndex();
    if (!editorPane_.tabHasFilePath(idx)) return;
    // No in-memory edits to lose: reload silently.
    if (!editorPane_.tabModified(idx)) {
        editorPane_.reloadTab(idx);
        commands_.commandStatusChanged();
        return;
    }
    juce::NativeMessageBox::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Revert to Saved",
        "Discard your changes to \"" + editorPane_.tabName(idx) + "\" and "
        "reload it from disk?",
        this,
        juce::ModalCallbackFunction::create([this, idx](int result) {
            if (result == 1) {  // 1 = OK, 0 = Cancel
                editorPane_.reloadTab(idx);
                commands_.commandStatusChanged();
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
    updateSessionDocumentPath();
    guiState_.asyncEval.launch(code.toStdString(), appCtx_, *session_,
                               flashStart, flashEnd);
}

// Anchor document-relative imports to the visible document: evaluated code
// can import a .x file sitting next to the notebook/tab it came from, with
// no project setup. An unsaved document has no path, so it clears the anchor
// (imports then use the search paths only).
void MainComponent::updateSessionDocumentPath() {
    if (!session_) return;
    // Never touch the session/TypeChecker while a background eval owns it
    // (the worker reads the source path during import resolution). A busy
    // eval also means no new eval will launch, so skipping is safe -- the
    // next successful launch re-anchors.
    if (guiState_.asyncEval.busy()) return;
    juce::File doc = docModeIsNotebook() ? notebook_->currentFile()
                                      : editorPane_.activeFile();
    session_->setDocumentPath(
        doc == juce::File() ? std::string{}
                            : doc.getFullPathName().toStdString());
}

// Where file dialogs should start: the visible document's directory, else
// the project, else home.
juce::File MainComponent::dialogDefaultDir() const {
    juce::File doc = docModeIsNotebook() ? notebook_->currentFile()
                                      : editorPane_.activeFile();
    if (doc != juce::File()) return doc.getParentDirectory();
    // The directory the last open/save dialog chose from (persisted).
    juce::File last(settings_.getValue("lastDialogDir"));
    if (last.isDirectory()) return last;
    if (!appCtx_.projectDir.empty()) {
        juce::File proj(juce::String(appCtx_.projectDir));
        if (proj.isDirectory()) return proj;
    }
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
}

void MainComponent::rememberDialogDir(juce::File const& chosen) {
    if (chosen == juce::File()) return;
    juce::File dir = chosen.getParentDirectory();
    if (dir.isDirectory())
        settings_.setValue("lastDialogDir", dir.getFullPathName());
}

// The stdlib modules directory (first existing system search path).
juce::File MainComponent::stdlibModulesDir() const {
    if (!appCtx_.moduleCompiler) return {};
    for (auto const& p : appCtx_.moduleCompiler->systemPaths()) {
        juce::File dir((juce::String(p)));
        if (dir.isDirectory()) return dir;
    }
    return {};
}

// The examples/ sibling of the stdlib modules directory -- present in an
// installed distribution folder, absent in a dev source tree (where the
// compiled-in fallbacks are lang/modules and bridge/modules).
juce::File MainComponent::distExamplesDir() const {
    juce::File mods = stdlibModulesDir();
    if (mods == juce::File()) return {};
    juce::File ex = mods.getParentDirectory().getChildFile("examples");
    return ex.isDirectory() ? ex : juce::File();
}

bool MainComponent::isDistExample(juce::File const& file) const {
    juce::File ex = distExamplesDir();
    return ex != juce::File() && file.isAChildOf(ex);
}

// Create <dir> with a tzpl-config file, a modules/ directory, and a starter
// notebook, then open the notebook (which registers the project).
void MainComponent::newProjectFlow() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "New Project (choose a folder name)",
        dialogDefaultDir().getChildFile("Untitled Project"), "");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode,
        [this](juce::FileChooser const& fc) {
            auto dir = fc.getResult();
            if (dir == juce::File()) return;
            rememberDialogDir(dir);
            confirmNotebookDiscardThen([this, dir] { createProject(dir); });
        });
}

void MainComponent::createProject(juce::File const& dir) {
    if (!dir.getChildFile("modules").createDirectory()) {
        logLine("could not create project at " + dir.getFullPathName());
        return;
    }
    juce::File config = dir.getChildFile("tzpl-config");
    if (!config.existsAsFile()) {
        config.replaceWithText(
            "-- Tzopilotl project config (key = value, `--` comments).\n"
            "-- Applied when the app is launched on a file in this project.\n"
            "-- silos = 2\n"
            "-- sampleRate = 48000\n"
            "-- bufferFrames = 512\n"
            "-- channels = 2\n",
            /*asUnicode=*/false, /*writeUnicodeHeaderBytes=*/false, "\n");
    }
    juce::File doc = dir.getChildFile("main.tzd");
    if (doc.existsAsFile()) {  // re-running over an existing project
        openPath(doc);
        return;
    }
    notebook_->newDocument();
    String err;
    if (!notebook_->saveToFile(doc, err)) {
        logLine("could not write " + doc.getFullPathName() + ": " + err);
        return;
    }
    showNotebook(true);
    appCtx_.projectDir = dir.getFullPathName().toStdString();
    registerProjectFor(doc);  // recents + modules/ on the search path
    logLine("project created: " + dir.getFullPathName());
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
        cmd::fileNew, cmd::fileNewNotebook, cmd::fileNewProject,
        cmd::fileOpen, cmd::fileOpenExample, cmd::fileRevealModules,
        cmd::helpAbout,
        cmd::fileSave, cmd::fileSaveAs, cmd::fileSaveCopy, cmd::fileRevert,
        cmd::fileClose,
#if !JUCE_MAC
        cmd::quit,
#endif
        cmd::editUndo, cmd::editRedo, cmd::editCut, cmd::editCopy,
        cmd::editPaste, cmd::editSelectAll, cmd::editClearOutput,
        cmd::editToggleComment, cmd::editIndent, cmd::editOutdent,
        cmd::findShow, cmd::findNext, cmd::findPrevious,
        cmd::findUseSelection, cmd::findUseSelectionReplace,
        cmd::fontIncrease, cmd::fontDecrease,
        cmd::viewEditor, cmd::viewNotebook, cmd::viewGraph, cmd::viewRotate,
        cmd::togglePerform, cmd::togglePluginBrowser,
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
    case cmd::fileNewProject:
        set("New Project...", "File");
        break;
    case cmd::fileOpenExample:
        set("Open Example...", "File");
        info.setActive(distExamplesDir() != juce::File());
        break;
    case cmd::fileRevealModules:
        set("Reveal Modules Folder", "File");
        info.setActive(stdlibModulesDir() != juce::File());
        break;
    case cmd::helpAbout:
        set("About Tzopilotl", "Help");
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
    case cmd::fileRevert:
        set("Revert to Saved", "File");
        // Only meaningful for a file-backed editor tab; the notebook keeps
        // its own history.
        info.setActive(!docModeIsNotebook() && editorPane_.activeHasFilePath());
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
    case cmd::viewEditor:
        set("Text Editor", "View");
        info.addDefaultKeypress('1', mod);
        info.setTicked(centerMode_ == CenterMode::editor);
        break;
    case cmd::viewNotebook:
        set("Notebook", "View");
        info.addDefaultKeypress('2', mod);
        info.setTicked(centerMode_ == CenterMode::notebook);
        break;
    case cmd::viewGraph:
        set("Node Graph", "View");
        info.addDefaultKeypress('3', mod);
        info.setTicked(centerMode_ == CenterMode::graph);
        break;
    case cmd::viewRotate:
        set("Next View", "View");
        info.addDefaultKeypress('\\', mod);
        break;
    case cmd::togglePerform:
        set("Perform Mode", "View");
        info.addDefaultKeypress('p', modShift);
        info.setTicked(performView_ != nullptr);
        break;
    case cmd::togglePluginBrowser:
        set("Plugin Browser", "View");
        info.addDefaultKeypress('b', modShift);
        info.setTicked(pluginBrowser_ != nullptr && pluginBrowser_->isVisible());
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
        // Only one notebook is open at a time: a new one replaces it.
        confirmNotebookDiscardThen([this] { openNewNotebook(); });
        return true;
    case cmd::fileOpen:
        openFileFlow();
        return true;
    case cmd::fileNewProject:
        newProjectFlow();
        return true;
    case cmd::fileOpenExample: {
        juce::File ex = distExamplesDir();
        if (ex == juce::File()) {
            logLine("no examples folder found next to the modules folder");
            return true;
        }
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Open Example", ex, "*.x;*.tzd");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
            [this](juce::FileChooser const& fc) {
                auto file = fc.getResult();
                if (file != juce::File()) openPath(file);
            });
        return true;
    }
    case cmd::fileRevealModules: {
        juce::File mods = stdlibModulesDir();
        if (mods == juce::File()) {
            logLine("no modules folder found");
        } else if (std::getenv("TZPL_JUCE_SELFTEST")) {
            // The selftest sweeps every command; don't open Finder windows.
            logLine("reveal skipped under selftest");
        } else {
            mods.revealToUser();
        }
        return true;
    }
    case cmd::helpAbout:
        showAboutBox();
        return true;
    case cmd::fileSave:
        if (docModeIsNotebook()) saveNotebookFlow(false);
        else saveActiveFlow(false);
        return true;
    case cmd::fileSaveAs:
        if (docModeIsNotebook()) saveNotebookFlow(true);
        else saveActiveFlow(true);
        return true;
    case cmd::fileSaveCopy: {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Save a Copy As",
            dialogDefaultDir().getChildFile(editorPane_.activeTabName()),
            "*.x");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](juce::FileChooser const& fc) {
                auto file = fc.getResult();
                if (file == juce::File()) return;
                rememberDialogDir(file);
                editorPane_.saveCopy(file);
            });
        return true;
    }
    case cmd::fileRevert:
        revertActiveFlow();
        return true;
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
        if (docModeIsNotebook()) notebook_->undoDocument();
        else editorPane_.undo();
        return true;
    case cmd::editRedo:
        if (docModeIsNotebook()) notebook_->redoDocument();
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
    case cmd::viewEditor:
        setCenterMode(CenterMode::editor);
        return true;
    case cmd::viewNotebook:
        setCenterMode(CenterMode::notebook);
        return true;
    case cmd::viewGraph:
        setCenterMode(CenterMode::graph);
        return true;
    case cmd::viewRotate:
        setCenterMode(centerMode_ == CenterMode::editor   ? CenterMode::notebook
                    : centerMode_ == CenterMode::notebook ? CenterMode::graph
                                                          : CenterMode::editor);
        return true;
    case cmd::togglePerform:
        togglePerform();
        return true;
    case cmd::togglePluginBrowser:
        togglePluginBrowser();
        return true;

    // -- Eval ---------------------------------------------------------------
    // With the notebook shown, Cmd+Enter / Shift+Enter run the focused cell
    // and Cmd+Shift+Enter runs every cell, matching the ImGui app.
    case cmd::evalSelection: {
        if (docModeIsNotebook()) { notebook_->runFocusedCell(); return true; }
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
        if (docModeIsNotebook()) { notebook_->runFocusedCell(); return true; }
        {
            int line = editorPane_.cursorLine();
            launchEval(editorPane_.getCurrentLineText(), line, line);
        }
        return true;
    case cmd::evalFile:
        if (docModeIsNotebook()) { notebook_->runAll(); return true; }
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
        // Drive the selftest's own slider ("a" on panel "t"): a startup
        // document may have materialized widgets ahead of it.
        if (w->kind == bridge::UIWidgetKind::Slider
            && w->name == "a" && w->panel == "t") {
            if (w->values.empty()) w->values.resize(1);
            w->values[0] = w->spec.map(0.75);
            w->dirtyEngine = true;
            return w->values[0];
        }
    }
    return -1.0;
}

void MainComponent::testShowDemo(String const& which) {
    if (which == "about") {
        showAboutBox();
    } else if (which == "cell-error-output") {
        // A cell eval error must make the cell's output pane appear (grow
        // the cell) without any other user interaction.
        showNotebook(true);
        notebook_->testTypeIntoFocusedCell("let nope = ;");
        notebook_->runFocusedCell();
        auto poll = std::make_shared<std::function<void(int)>>();
        *poll = [this, poll](int triesLeft) {
            if (notebook_->testFocusedCellOutput().isEmpty() && triesLeft > 0) {
                juce::Timer::callAfterDelay(
                    200, [poll, triesLeft] { (*poll)(triesLeft - 1); });
                return;
            }
            bool haveLines = notebook_->testFocusedCellOutput().isNotEmpty();
            bool shown = notebook_->testFocusedCellOutputPaneShown();
            String verdict = String("cell-error-output: lines=")
                + (haveLines ? "yes" : "no")
                + " paneShown=" + (shown ? "1" : "0")
                + (haveLines && shown ? " OK" : " FAIL");
            logLine(verdict);
            std::fprintf(stderr, "%s\n", verdict.toRawUTF8());
        };
        (*poll)(50);
    } else if (which == "collapse-sliver") {
        // Collapsing a cell must hide its editor completely -- no sliver of
        // code under the header.
        showNotebook(true);
        notebook_->testTypeIntoFocusedCell("import ui.*;\nlet x = 1;\nlet y = 2;");
        notebook_->testCollapseFocusedCell(true);
        bool vis = notebook_->testFocusedCellEditorVisible();
        String verdict = String("collapse-sliver: editorVisible=")
            + (vis ? "1 FAIL" : "0 OK");
        logLine(verdict);
        std::fprintf(stderr, "%s\n", verdict.toRawUTF8());
    } else if (which.startsWith("dialog-dir")) {
        // "dialog-dir:<dir>" -- print where dialogs would start, then
        // remember <dir> as if a chooser completed there. Run twice to
        // check the directory persists across launches.
        std::fprintf(stderr, "dialog-dir: start=%s\n",
                     dialogDefaultDir().getFullPathName().toRawUTF8());
        auto arg = which.fromFirstOccurrenceOf(":", false, false);
        if (arg.isNotEmpty()) {
            rememberDialogDir(juce::File(arg).getChildFile("chosen.x"));
            settings_.saveIfNeeded();  // the demo runner kills the process
        }
    } else if (which == "claimed-never-float") {
        // Panels claimed by the open notebook must not become floating
        // controls windows while the notebook is hidden. Wait for the
        // opened document's widgets, hide the notebook, let the timer's
        // refreshControlsWindows() run, and report what happened.
        auto poll = std::make_shared<std::function<void(int)>>();
        *poll = [this, poll](int triesLeft) {
            if (testWidgetCount() == 0 && triesLeft > 0) {
                juce::Timer::callAfterDelay(
                    200, [poll, triesLeft] { (*poll)(triesLeft - 1); });
                return;
            }
            showNotebook(false);
            juce::Timer::callAfterDelay(500, [this, poll] {
                int floats = (int)controlsWindows_.size();
                showNotebook(true);
                int widgets = testWidgetCount();
                String verdict = String("claimed-never-float: floats=")
                    + String(floats) + " widgets=" + String(widgets)
                    + (floats == 0 && widgets > 0 ? " OK" : " FAIL");
                logLine(verdict);
                std::fprintf(stderr, "%s\n", verdict.toRawUTF8());
            });
        };
        (*poll)(50);
    } else if (which == "find") {
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
    } else if (which == "prose-add") {
        // Prose cell + typing + a new Panel cell: the prose text must survive
        // the rebuild that adding a cell triggers.
        showNotebook(true);
        notebook_->testAddCell(doc::CellKind::Prose);
        notebook_->testTypeIntoFocusedCell("prose text must survive");
        int prose = notebook_->testCellCount() - 1;
        notebook_->testAddCell(doc::CellKind::Panel);
        String after = notebook_->testCellEditorText(prose);
        String verdict = after == "prose text must survive"
                             ? "prose-add: prose text survived"
                             : "prose-add: PROSE TEXT LOST -- got \"" + after + "\"";
        logLine(verdict);
        std::fprintf(stderr, "%s\n", verdict.toRawUTF8());   // scriptable
    } else if (which == "new-notebook-dirty") {
        // A dirty notebook must not be replaced without asking: the box
        // appears and the typed cell is still there behind it.
        showNotebook(true);
        notebook_->testTypeIntoFocusedCell("notebook text must survive");
        juce::Timer::callAfterDelay(300, [this] {
            // Drive the real menu command, not the helper directly.
            juce::ApplicationCommandTarget::InvocationInfo info(
                cmd::fileNewNotebook);
            perform(info);
            juce::Timer::callAfterDelay(600, [this] {
                String after = notebook_->testCellEditorText(0);
                String verdict =
                    after == "notebook text must survive"
                        ? "new-notebook-dirty: notebook intact, box is up"
                        : "new-notebook-dirty: NOTEBOOK WIPED -- got \""
                              + after + "\"";
                std::fprintf(stderr, "%s\n", verdict.toRawUTF8());
            });
        });
    } else if (which == "quit-dirty") {
        // Dirty the untitled tab, then take the real quit path: the
        // unsaved-changes box must appear rather than the app just quitting.
        testTypeIntoEditor("let dirty = 1;");
        juce::Timer::callAfterDelay(300, [] {
            juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
        });
    } else if (which == "quit-dirty-notebook") {
        // Same, but the unsaved state is a notebook cell edit: quitting
        // must prompt rather than silently dropping the notebook.
        showNotebook(true);
        notebook_->testTypeIntoFocusedCell("notebook text must survive");
        juce::Timer::callAfterDelay(300, [this] {
            std::fprintf(stderr, "quit-dirty-notebook: isModified=%d\n",
                         notebook_ && notebook_->isModified() ? 1 : 0);
            juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
            juce::Timer::callAfterDelay(600, [] {
                std::fprintf(stderr,
                             "quit-dirty-notebook: still running (box is up)\n");
            });
        });
    } else if (which == "quit-dirty-notebook-ran") {
        // Edit a cell, RUN it, then quit: still unsaved work, must prompt.
        showNotebook(true);
        notebook_->testTypeIntoFocusedCell("let quitDemo = 42;");
        notebook_->runFocusedCell();
        juce::Timer::callAfterDelay(1500, [this] {
            std::fprintf(stderr, "quit-dirty-notebook-ran: isModified=%d\n",
                         notebook_ && notebook_->isModified() ? 1 : 0);
            juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
            juce::Timer::callAfterDelay(600, [] {
                std::fprintf(stderr,
                             "quit-dirty-notebook-ran: still running (box is up)\n");
            });
        });
    } else if (which == "quit-dirty-last") {
        // The user's exact repro: open a .tzd (given on the command line),
        // edit the LAST cell's text, Cmd+Q. Waits out the run-on-load evals
        // and the typing coalesce, then takes the real quit path.
        showNotebook(true);
        juce::Timer::callAfterDelay(2000, [this] {
            int last = notebook_->testCellCount() - 1;
            String text = notebook_->testCellEditorText(last);
            bool edited = notebook_->testTypeIntoCell(
                last, text.replace("play(5.0)", "play(5.1)"));
            std::fprintf(stderr, "quit-dirty-last: edited=%d cell=%d\n",
                         edited ? 1 : 0, last);
            juce::Timer::callAfterDelay(1500, [this] {
                std::fprintf(stderr, "quit-dirty-last: isModified=%d\n",
                             notebook_ && notebook_->isModified() ? 1 : 0);
                juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
                juce::Timer::callAfterDelay(600, [] {
                    std::fprintf(stderr,
                                 "quit-dirty-last: still running (box is up)\n");
                });
            });
        });
    } else if (which == "edit-last-only") {
        // Edit the last cell and stop -- external harness then delivers a
        // real Cmd+Q keystroke to test the actual menu/key quit routing.
        showNotebook(true);
        juce::Timer::callAfterDelay(2000, [this] {
            int last = notebook_->testCellCount() - 1;
            String text = notebook_->testCellEditorText(last);
            bool edited = notebook_->testTypeIntoCell(
                last, text.replace("play(5.0)", "play(5.1)"));
            std::fprintf(stderr, "edit-last-only: edited=%d\n", edited ? 1 : 0);
        });
    } else if (which == "history-ops") {
        // Typing, add cell, delete cell must each land a history node.
        showNotebook(true);
        notebook_->testTypeIntoFocusedCell("let a = 1;");
        juce::Timer::callAfterDelay(1600, [this] {
            notebook_->testAddCell(doc::CellKind::Prose);
            notebook_->deleteSelectedCell();
            juce::String labels;
            for (auto const& r : notebook_->historyRows())
                labels << r.label << " | ";
            std::fprintf(stderr, "history-ops: %s\n", labels.toRawUTF8());
        });
    } else if (which == "quit-dirty-save") {
        // What the box's Save button runs: an untitled tab must get a Save
        // As chooser, and cancelling it must report failure (not "saved").
        testTypeIntoEditor("let dirty = 1;");
        juce::Timer::callAfterDelay(300, [this] {
            saveAllTabsThen([this](bool ok) {
                logLine(ok ? "quit-dirty-save: all tabs saved"
                           : "quit-dirty-save: save cancelled or failed");
            });
        });
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
