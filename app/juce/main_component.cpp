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

namespace tzplapp {

using juce::String;

// ---------------------------------------------------------------------------
// OutputLog
// ---------------------------------------------------------------------------

OutputLog::OutputLog() {
    text_.setMultiLine(true);
    text_.setReadOnly(true);
    text_.setScrollbarsShown(true);
    text_.setCaretVisible(false);
    text_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                    cmd::kEditorFontSizes[0], juce::Font::plain));
    addAndMakeVisible(text_);
}

void OutputLog::appendLine(String const& line) {
    text_.moveCaretToEnd();
    text_.insertTextAtCaret(line + "\n");
}

void OutputLog::setFontSize(float px) {
    text_.applyFontToAllText(
        juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), px,
                          juce::Font::plain));
}

void OutputLog::lookAndFeelChanged() {
    text_.applyColourToAllText(
        findColour(juce::TextEditor::textColourId), true);
    text_.setColour(juce::TextEditor::backgroundColourId,
                    findColour(juce::TextEditor::backgroundColourId));
}

// ---------------------------------------------------------------------------
// MainComponent
// ---------------------------------------------------------------------------

MainComponent::MainComponent(bridge::AppContext& appCtx,
                             juce::ApplicationCommandManager& commands,
                             TzplLookAndFeel& lookAndFeel,
                             juce::PropertiesFile& settings)
    : appCtx_(appCtx), commands_(commands), lookAndFeel_(lookAndFeel),
      settings_(settings)
{
    centerPane_.setText("editor pane (M2)", juce::dontSendNotification);
    centerPane_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(centerPane_);
    addAndMakeVisible(outputLog_);

    // Vertical split: center pane | resizer | output console. Mirrors the
    // ImGui app's splitRatio (editor fraction, default 0.7, clamped .2-.9).
    double ratio = settings_.getDoubleValue("splitRatio", 0.7);
    ratio = juce::jlimit(0.2, 0.9, ratio);
    layout_.setItemLayout(0, 80.0, -0.9, -ratio);         // center pane
    layout_.setItemLayout(1, 8.0, 8.0, 8.0);              // resizer bar
    layout_.setItemLayout(2, 60.0, -0.8, -(1.0 - ratio)); // output
    resizer_ = std::make_unique<juce::StretchableLayoutResizerBar>(
        &layout_, 1, /*vertical bar=*/false);
    addAndMakeVisible(*resizer_);

    applyTheme(settings_.getIntValue("theme", themeDark));
    applyFontIndex(settings_.getIntValue("fontIndex", 0));

    logLine("Tzopilotl. Cmd+Enter: eval block, Shift+Enter: eval line, "
            "Cmd+Shift+Enter: eval file.");
}

MainComponent::~MainComponent() {
    saveSplitRatio();
}

void MainComponent::resized() {
    juce::Component* comps[] = { &centerPane_, resizer_.get(), &outputLog_ };
    layout_.layOutComponents(comps, 3, 0, 0, getWidth(), getHeight(),
                             /*vertically=*/true, /*resizeOther=*/true);
    saveSplitRatio();
}

void MainComponent::saveSplitRatio() {
    if (getHeight() <= 0) return;
    double ratio = (double)centerPane_.getHeight() / getHeight();
    if (ratio > 0.05 && ratio < 0.95)
        settings_.setValue("splitRatio", ratio);
}

void MainComponent::logLine(String const& line) {
    outputLog_.appendLine(line);
}

void MainComponent::confirmUnsavedChangesThen(std::function<void()> proceed) {
    // M1: no documents can hold unsaved changes yet; the editor (M2) and
    // notebook (M3) hook their dirty state in here. The async shape is the
    // one all close/quit flows share.
    bool hasUnsavedChanges = false;
    String description;

    if (!hasUnsavedChanges) {
        proceed();
        return;
    }

    juce::NativeMessageBox::showYesNoCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Unsaved Changes",
        "Do you want to save changes to " + description + "?\n"
        "Your changes will be lost if you don't save them.",
        this,
        juce::ModalCallbackFunction::create([proceed](int result) {
            // 1 = yes/save, 2 = no/don't save, 0 = cancel
            if (result == 1) { /* M2+: save all, then */ proceed(); }
            else if (result == 2) proceed();
        }));
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

    // Font-size / theme ranges
    if (id >= cmd::fontSetBase && id < cmd::fontSetBase + cmd::kNumEditorFontSizes) {
        applyFontIndex(id - cmd::fontSetBase);
        return true;
    }
    if (id >= cmd::themeSetBase && id < cmd::themeSetBase + themeCount) {
        applyTheme(id - cmd::themeSetBase);
        return true;
    }

    // M1: the panels these commands drive arrive in M2 (editor) and M3
    // (notebook); until then the commands land in the console so the whole
    // menu/shortcut path is verifiable end to end.
    auto todo = [&](char const* name) {
        logLine(String("[command] ") + name);
        return true;
    };

    switch (id) {
    case cmd::fileNew:          return todo("File > New");
    case cmd::fileNewNotebook:  return todo("File > New Notebook");
    case cmd::fileOpen: {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Open", juce::File(), "*.x;*.tzd");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::canSelectDirectories,
            [this](juce::FileChooser const& fc) {
                auto file = fc.getResult();
                if (file != juce::File())
                    logLine("[open] " + file.getFullPathName());
            });
        return true;
    }
    case cmd::fileSave:         return todo("File > Save");
    case cmd::fileSaveAs: {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Save As", juce::File::getSpecialLocation(
                           juce::File::userHomeDirectory)
                           .getChildFile("untitled.x"),
            "*.x");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](juce::FileChooser const& fc) {
                auto file = fc.getResult();
                if (file != juce::File())
                    logLine("[save as] " + file.getFullPathName());
            });
        return true;
    }
    case cmd::fileSaveCopy:     return todo("File > Save a Copy As...");
    case cmd::fileClose:        return todo("File > Close Tab");
    case cmd::quit:
        confirmUnsavedChangesThen(
            [] { juce::JUCEApplicationBase::quit(); });
        return true;

    case cmd::editUndo:         return todo("Edit > Undo");
    case cmd::editRedo:         return todo("Edit > Redo");
    case cmd::editCut:          return todo("Edit > Cut");
    case cmd::editCopy:         return todo("Edit > Copy");
    case cmd::editPaste:        return todo("Edit > Paste");
    case cmd::editSelectAll:    return todo("Edit > Select All");
    case cmd::editClearOutput:
        outputLog_.clear();
        return true;
    case cmd::editToggleComment: return todo("Edit > Toggle Line Comment");
    case cmd::editIndent:       return todo("Edit > Indent");
    case cmd::editOutdent:      return todo("Edit > Outdent");

    case cmd::findShow:         return todo("Find > Find...");
    case cmd::findNext:         return todo("Find > Find Next");
    case cmd::findPrevious:     return todo("Find > Find Previous");
    case cmd::findUseSelection: return todo("Find > Use Selection for Find");
    case cmd::findUseSelectionReplace:
        return todo("Find > Use Selection for Replace");

    case cmd::fontIncrease:
        applyFontIndex(fontIndex_ + 1);
        return true;
    case cmd::fontDecrease:
        applyFontIndex(fontIndex_ - 1);
        return true;
    case cmd::toggleNotebookView: return todo("View > Toggle Notebook / Editor");

    case cmd::evalSelection:    return todo("Eval Selection (Cmd+Enter)");
    case cmd::evalLine:         return todo("Eval Line (Shift+Enter)");
    case cmd::evalFile:         return todo("Eval File (Cmd+Shift+Enter)");
    }
    return false;
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
    outputLog_.setFontSize(cmd::kEditorFontSizes[fontIndex_]);
    commands_.commandStatusChanged();
}

}
