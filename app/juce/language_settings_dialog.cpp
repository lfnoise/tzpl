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
//  language_settings_dialog.cpp
//  app (JUCE)
//

#include "language_settings_dialog.hpp"

#include "app_config.hpp"
#include "project_paths.hpp"

namespace tzplapp {

namespace {

constexpr int kLabelW = 196;
constexpr int kRowH = 26;
constexpr int kRowGap = 5;
constexpr int kSectionGap = 12;

// ---------------------------------------------------------------------------
// The scrolling form: rows only. The enclosing component owns the note and
// the buttons so they stay visible while the rows scroll.
// ---------------------------------------------------------------------------

class LangSettingsForm : public juce::Component {
public:
    explicit LangSettingsForm(std::string projectDir)
        : projectDir_(std::move(projectDir))
    {
        addRow(targetLabel_, "Settings file", targetBox_);
        if (!projectDir_.empty()) {
            targetBox_.addItem("Project -- "
                                   + juce::File(juce::String(projectDir_))
                                         .getFileName(),
                               1);
        }
        targetBox_.addItem("All projects (this user)", 2);
        targetBox_.setSelectedId(projectDir_.empty() ? 2 : 1,
                                 juce::dontSendNotification);
        targetBox_.onChange = [this] { loadFromTarget(); };

        pathLabel_.setJustificationType(juce::Justification::centredLeft);
        pathLabel_.setFont(juce::Font(juce::FontOptions(11.f)));
        pathLabel_.setMinimumHorizontalScale(0.5f);  // long paths shrink, not clip
        addAndMakeVisible(pathLabel_);

        addSection(memSection_, "Memory");
        addRow(nrtHeapLabel_, "Main VM heap (MB)", nrtHeap_);
        addRow(siloHeapLabel_, "Silo VM heap (MB)", siloHeap_);
        addRow(chunkMinLabel_, "Heap growth chunk min (MB)", chunkMin_);
        addRow(chunkMaxLabel_, "Heap growth chunk max (MB)", chunkMax_);

        addSection(execSection_, "Execution Limits");
        addRow(regsLabel_, "Register file (words)", regs_);
        addRow(framesLabel_, "Call depth (frames)", frames_);
        addRow(dynLabel_, "Dynamic scope entries", dyn_);
        addRow(dynWordsLabel_, "Dynamic scope payload (words)", dynWords_);

        addSection(gcSection_, "Garbage Collector");
        addRow(gcBudgetLabel_, "Step budget (microseconds)", gcBudget_);
        addRow(gcTriggerLabel_, "Cycle trigger floor (allocs)", gcTrigger_);
        addRow(gcGrowthLabel_, "Heap growth factor", gcGrowth_);
        addRow(mmuLabel_, "Silo MMU governor", mmuBox_);
        mmuBox_.addItem("off", 1);
        mmuBox_.addItem("on", 2);
        addRow(mmuPctLabel_, "MMU mutator share (%)", mmuPct_);
        addRow(mmuWindowLabel_, "MMU window (ms)", mmuWindow_);

        addSection(limitSection_, "Traversal Limits");
        addRow(graphDepthLabel_, "Graph depth (==, hash, serialize)", graphDepth_);
        addRow(lazyForceLabel_, "Lazy force limit", lazyForce_);
        addRow(printDepthLabel_, "Print depth", printDepth_);
        addRow(listPrintLabel_, "List print limit", listPrint_);
        limitHint_.setText("Programs can also adjust these live via the"
                           " setGraphMaxDepth family of builtins.",
                           juce::dontSendNotification);
        limitHint_.setJustificationType(juce::Justification::centredLeft);
        limitHint_.setFont(juce::Font(juce::FontOptions(11.f)));
        limitHint_.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(limitHint_);

        for (auto* e : numericFields()) e->setInputRestrictions(9, "0123456789");

        loadFromTarget();
        setSize(520, rowsBottom());
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);

        for (auto const& item : items_) {
            if (item.editor == nullptr) {           // section header
                area.removeFromTop(kSectionGap);
                item.label->setBounds(area.removeFromTop(kRowH - 4));
                area.removeFromTop(2);
                continue;
            }
            auto row = area.removeFromTop(kRowH);
            item.label->setBounds(row.removeFromLeft(kLabelW).reduced(0, 1));
            row.removeFromLeft(8);
            item.editor->setBounds(row.reduced(0, 1));
            area.removeFromTop(kRowGap);
            if (item.editor == &targetBox_) {
                pathLabel_.setBounds(area.removeFromTop(18)
                                         .withTrimmedLeft(kLabelW + 8));
                area.removeFromTop(kRowGap);
            }
            if (item.editor == &listPrint_) {
                limitHint_.setBounds(area.removeFromTop(18));
                area.removeFromTop(kRowGap);
            }
        }
    }

    int rowsBottom() const {
        int y = 12 + 12;  // reduced() top + bottom margins
        for (auto const& item : items_) {
            if (item.editor == nullptr) { y += kSectionGap + kRowH - 2; continue; }
            y += kRowH + kRowGap;
            if (item.editor == &targetBox_ || item.editor == &listPrint_)
                y += 18 + kRowGap;
        }
        return y;
    }

    juce::File targetFile() const {
        if (targetBox_.getSelectedId() == 1)
            return juce::File(juce::String(projectDir_)).getChildFile("tzpl-config");
        return juce::File(juce::String(userConfigFile()));
    }

    // Show what the chosen file will actually produce at launch: defaults,
    // then the user file, then the project file when that is the target.
    void loadFromTarget() {
        AppConfig cfg;
        if (std::string user = userConfigFile(); !user.empty())
            loadConfigFile(user, cfg);
        if (targetBox_.getSelectedId() == 1)
            loadConfigFile(targetFile().getFullPathName().toStdString(), cfg);
        setFields(cfg);
        pathLabel_.setText(targetFile().getFullPathName(),
                           juce::dontSendNotification);
    }

    // Reset every field to the built-in defaults (not the file's contents).
    void resetToDefaults() { setFields(AppConfig{}); }

    bool save(std::function<void(juce::String const&)> const& log,
              juce::Component* messageBoxParent) {
        AppConfig cfg;
        // Start from the file being written so unknown-to-the-form state
        // (a key added by hand) round-trips untouched.
        std::string path = targetFile().getFullPathName().toStdString();
        loadConfigFile(path, cfg);

        cfg.langNrtHeapMB = intOf(nrtHeap_, cfg.langNrtHeapMB, 1);
        cfg.langSiloHeapMB = intOf(siloHeap_, cfg.langSiloHeapMB, 1);
        cfg.langHeapChunkMinMB = intOf(chunkMin_, cfg.langHeapChunkMinMB, 1);
        cfg.langHeapChunkMaxMB = intOf(chunkMax_, cfg.langHeapChunkMaxMB,
                                       cfg.langHeapChunkMinMB);
        cfg.langMaxRegisters = intOf(regs_, cfg.langMaxRegisters, 256);
        cfg.langMaxCallDepth = intOf(frames_, cfg.langMaxCallDepth, 16);
        cfg.langMaxDynScope = intOf(dyn_, cfg.langMaxDynScope, 16);
        cfg.langMaxDynScopeWords = intOf(dynWords_, cfg.langMaxDynScopeWords, 16);
        cfg.langGcStepBudgetUs = intOf(gcBudget_, cfg.langGcStepBudgetUs, 1);
        cfg.langGcMinTriggerAllocs = intOf(gcTrigger_, cfg.langGcMinTriggerAllocs, 1);
        cfg.langGcGrowthFactor = intOf(gcGrowth_, cfg.langGcGrowthFactor, 1);
        cfg.langSiloMmu = mmuBox_.getSelectedId() == 2 ? 1 : 0;
        cfg.langSiloMmuMutatorPct =
            juce::jmin(100, intOf(mmuPct_, cfg.langSiloMmuMutatorPct, 0));
        cfg.langSiloMmuWindowMs = intOf(mmuWindow_, cfg.langSiloMmuWindowMs, 1);
        cfg.langGraphMaxDepth = intOf(graphDepth_, cfg.langGraphMaxDepth, 1);
        cfg.langLazyForceLimit = intOf(lazyForce_, cfg.langLazyForceLimit, 1);
        cfg.langPrintMaxDepth = intOf(printDepth_, cfg.langPrintMaxDepth, 1);
        cfg.langListPrintLimit = intOf(listPrint_, cfg.langListPrintLimit, 1);

        std::string err;
        if (!saveConfigFile(path, cfg, &err)) {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon, "Could not save settings",
                juce::String(err), messageBoxParent);
            return false;
        }
        if (log) log("language settings saved: " + juce::String(path)
                     + " (applies on relaunch)");
        return true;
    }

private:
    struct Row { juce::Label* label; juce::Component* editor; };

    void addRow(juce::Label& label, juce::String const& text,
                juce::Component& editor) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
        addAndMakeVisible(editor);
        items_.push_back({&label, &editor});
    }

    void addSection(juce::Label& label, juce::String const& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(13.f, juce::Font::bold)));
        addAndMakeVisible(label);
        items_.push_back({&label, nullptr});
    }

    std::vector<juce::TextEditor*> numericFields() {
        return {&nrtHeap_, &siloHeap_, &chunkMin_, &chunkMax_,
                &regs_, &frames_, &dyn_, &dynWords_,
                &gcBudget_, &gcTrigger_, &gcGrowth_, &mmuPct_, &mmuWindow_,
                &graphDepth_, &lazyForce_, &printDepth_, &listPrint_};
    }

    void setFields(AppConfig const& cfg) {
        nrtHeap_.setText(juce::String(cfg.langNrtHeapMB), false);
        siloHeap_.setText(juce::String(cfg.langSiloHeapMB), false);
        chunkMin_.setText(juce::String(cfg.langHeapChunkMinMB), false);
        chunkMax_.setText(juce::String(cfg.langHeapChunkMaxMB), false);
        regs_.setText(juce::String(cfg.langMaxRegisters), false);
        frames_.setText(juce::String(cfg.langMaxCallDepth), false);
        dyn_.setText(juce::String(cfg.langMaxDynScope), false);
        dynWords_.setText(juce::String(cfg.langMaxDynScopeWords), false);
        gcBudget_.setText(juce::String(cfg.langGcStepBudgetUs), false);
        gcTrigger_.setText(juce::String(cfg.langGcMinTriggerAllocs), false);
        gcGrowth_.setText(juce::String(cfg.langGcGrowthFactor), false);
        mmuBox_.setSelectedId(cfg.langSiloMmu != 0 ? 2 : 1,
                              juce::dontSendNotification);
        mmuPct_.setText(juce::String(cfg.langSiloMmuMutatorPct), false);
        mmuWindow_.setText(juce::String(cfg.langSiloMmuWindowMs), false);
        graphDepth_.setText(juce::String(cfg.langGraphMaxDepth), false);
        lazyForce_.setText(juce::String(cfg.langLazyForceLimit), false);
        printDepth_.setText(juce::String(cfg.langPrintMaxDepth), false);
        listPrint_.setText(juce::String(cfg.langListPrintLimit), false);
    }

    static int intOf(juce::TextEditor const& e, int fallback, int lo) {
        auto t = e.getText().trim();
        return t.isEmpty() ? fallback : juce::jmax(lo, t.getIntValue());
    }

    std::string projectDir_;
    std::vector<Row> items_;

    juce::Label targetLabel_, pathLabel_, limitHint_;
    juce::ComboBox targetBox_;
    juce::Label memSection_, execSection_, gcSection_, limitSection_;
    juce::Label nrtHeapLabel_, siloHeapLabel_, chunkMinLabel_, chunkMaxLabel_;
    juce::TextEditor nrtHeap_, siloHeap_, chunkMin_, chunkMax_;
    juce::Label regsLabel_, framesLabel_, dynLabel_, dynWordsLabel_;
    juce::TextEditor regs_, frames_, dyn_, dynWords_;
    juce::Label gcBudgetLabel_, gcTriggerLabel_, gcGrowthLabel_;
    juce::Label mmuLabel_, mmuPctLabel_, mmuWindowLabel_;
    juce::TextEditor gcBudget_, gcTrigger_, gcGrowth_, mmuPct_, mmuWindow_;
    juce::ComboBox mmuBox_;
    juce::Label graphDepthLabel_, lazyForceLabel_, printDepthLabel_, listPrintLabel_;
    juce::TextEditor graphDepth_, lazyForce_, printDepth_, listPrint_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LangSettingsForm)
};

// ---------------------------------------------------------------------------
// Dialog content: the form in a viewport (the row list is taller than a
// laptop screen), with the note and buttons pinned below it.
// ---------------------------------------------------------------------------

class LanguageSettingsComponent : public juce::Component {
public:
    LanguageSettingsComponent(std::string projectDir,
                              std::function<void(juce::String const&)> log,
                              std::function<void()> relaunch)
        : log_(std::move(log)), relaunch_(std::move(relaunch)),
          form_(std::move(projectDir))
    {
        viewport_.setViewedComponent(&form_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);

        noteLabel_.setText("Advanced. Read at startup: saving changes nothing"
                           " until you relaunch.", juce::dontSendNotification);
        noteLabel_.setJustificationType(juce::Justification::centredLeft);
        noteLabel_.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(noteLabel_);

        cancelButton_.onClick = [this] { closeWindow(); };
        defaultsButton_.onClick = [this] { form_.resetToDefaults(); };
        saveButton_.onClick = [this] { if (form_.save(log_, this)) closeWindow(); };
        saveRelaunchButton_.onClick = [this] {
            if (!form_.save(log_, this)) return;
            closeWindow();
            if (relaunch_) relaunch_();
        };
        addAndMakeVisible(cancelButton_);
        addAndMakeVisible(defaultsButton_);
        addAndMakeVisible(saveButton_);
        addAndMakeVisible(saveRelaunchButton_);

        int formH = form_.rowsBottom();
        // Fit small screens; the viewport scrolls when the form is taller.
        int viewH = juce::jmin(formH, 620);
        setSize(548, viewH + 78);
    }

    void resized() override {
        auto area = getLocalBounds();
        auto bottom = area.removeFromBottom(78).reduced(14, 12);
        viewport_.setBounds(area);
        form_.setSize(viewport_.getMaximumVisibleWidth(), form_.rowsBottom());

        noteLabel_.setBounds(bottom.removeFromTop(24));
        bottom.removeFromTop(4);
        auto buttons = bottom.removeFromTop(26);
        saveRelaunchButton_.setBounds(buttons.removeFromRight(140));
        buttons.removeFromRight(8);
        saveButton_.setBounds(buttons.removeFromRight(80));
        buttons.removeFromRight(8);
        cancelButton_.setBounds(buttons.removeFromRight(80));
        defaultsButton_.setBounds(buttons.removeFromLeft(90));
    }

private:
    void closeWindow() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    }

    std::function<void(juce::String const&)> log_;
    std::function<void()> relaunch_;
    LangSettingsForm form_;
    juce::Viewport viewport_;
    juce::Label noteLabel_;
    juce::TextButton cancelButton_{"Cancel"}, defaultsButton_{"Defaults"},
                     saveButton_{"Save"}, saveRelaunchButton_{"Save & Relaunch"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LanguageSettingsComponent)
};

juce::Component::SafePointer<juce::DialogWindow> gWindow;

}  // namespace

void showLanguageSettings(std::string const& projectDir,
                          std::function<void(juce::String const&)> log,
                          std::function<void()> relaunch) {
    if (gWindow != nullptr) {
        gWindow->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(new LanguageSettingsComponent(projectDir, std::move(log),
                                                     std::move(relaunch)));
    o.dialogTitle = "Language Settings";
    o.dialogBackgroundColour = juce::LookAndFeel::getDefaultLookAndFeel()
        .findColour(juce::ResizableWindow::backgroundColourId);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    gWindow = o.launchAsync();  // deletes itself when closed
}

void closeLanguageSettings() {
    // launchAsync() entered a modal state with deleteWhenDismissed set, so
    // leaving it is what frees the window.
    if (gWindow != nullptr) gWindow->exitModalState(0);
}

}  // namespace tzplapp
