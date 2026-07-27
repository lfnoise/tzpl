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
//  settings_dialog.cpp
//  app (JUCE)
//

#include "settings_dialog.hpp"

#include "app_config.hpp"
#include "project_paths.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

namespace tzplapp {

namespace {

constexpr int kLabelW = 168;
constexpr int kRowH = 26;
constexpr int kRowGap = 5;
constexpr int kSectionGap = 12;

// Device names as JUCE sees them. Scanned once per app run: opening the
// dialog must not re-enumerate CoreAudio behind a running stream more
// often than it has to.
struct DeviceNames {
    juce::StringArray outputs, inputs;
};

DeviceNames const& deviceNames() {
    static DeviceNames names = [] {
        DeviceNames n;
        juce::AudioDeviceManager dm;
        for (auto* type : dm.getAvailableDeviceTypes()) {
            type->scanForDevices();
            n.outputs.addArray(type->getDeviceNames(false));
            n.inputs.addArray(type->getDeviceNames(true));
        }
        n.outputs.removeDuplicates(false);
        n.inputs.removeDuplicates(false);
        return n;
    }();
    return names;
}

// ---------------------------------------------------------------------------

class SettingsComponent : public juce::Component {
public:
    SettingsComponent(std::string projectDir,
                      std::function<void(juce::String const&)> log,
                      std::function<void()> relaunch)
        : projectDir_(std::move(projectDir)), log_(std::move(log)),
          relaunch_(std::move(relaunch))
    {
        // Where the values are written. The project file (when there is a
        // project) wins over the user one at launch, so it is the default.
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

        addSection(outSection_, "Audio Output");
        addRow(deviceLabel_, "Output device", deviceBox_);
        deviceBox_.addItem("default", 1);
        for (auto const& d : deviceNames().outputs)
            deviceBox_.addItem(d, deviceBox_.getNumItems() + 1);
        addRow(channelsLabel_, "Output channels", channels_);
        addRow(firstChannelLabel_, "First output channel", firstChannel_);

        addSection(inSection_, "Audio Input");
        addRow(inDeviceLabel_, "Input device", inDeviceBox_);
        inDeviceBox_.addItem("same as output", 1);
        for (auto const& d : deviceNames().inputs)
            inDeviceBox_.addItem(d, inDeviceBox_.getNumItems() + 1);
        addRow(inChannelsLabel_, "Input channels", inChannels_);
        addRow(firstInChannelLabel_, "First input channel", firstInChannel_);
        inputHint_.setText("0 input channels means the Audio In node has no"
                           " outlet to patch from.",
                           juce::dontSendNotification);
        inputHint_.setJustificationType(juce::Justification::centredLeft);
        inputHint_.setMinimumHorizontalScale(1.f);
        addAndMakeVisible(inputHint_);

        addSection(engineSection_, "Engine");
        addRow(sampleRateLabel_, "Sample rate", sampleRateBox_);
        for (int hz : {44100, 48000, 88200, 96000, 192000})
            sampleRateBox_.addItem(juce::String(hz), sampleRateBox_.getNumItems() + 1);
        sampleRateBox_.setEditableText(true);
        addRow(bufferLabel_, "Buffer frames", bufferBox_);
        for (int f : {32, 64, 128, 256, 512, 1024, 2048})
            bufferBox_.addItem(juce::String(f), bufferBox_.getNumItems() + 1);
        bufferBox_.setEditableText(true);
        addRow(silosLabel_, "Silos (audio threads)", silos_);
        addRow(clocksLabel_, "Tempo clocks per silo", tempoClocks_);

#if TZPL_HAS_OSC || TZPL_HAS_NATS
        addSection(netSection_, "Network");
#endif
#if TZPL_HAS_OSC
        addRow(oscLabel_, "OSC port (0 = off)", oscPort_);
#endif
#if TZPL_HAS_NATS
        addRow(natsLabel_, "NATS URL", natsUrl_);
        addRow(engineNameLabel_, "Engine name", engineName_);
#endif

        for (auto* e : {&channels_, &firstChannel_, &inChannels_,
                        &firstInChannel_, &silos_, &tempoClocks_, &oscPort_}) {
            e->setInputRestrictions(6, "0123456789");
        }

        noteLabel_.setText("Read at startup: saving changes nothing until you"
                           " relaunch.", juce::dontSendNotification);
        noteLabel_.setJustificationType(juce::Justification::centredLeft);
        noteLabel_.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(noteLabel_);

        cancelButton_.onClick = [this] { closeWindow(); };
        saveButton_.onClick = [this] { if (save()) closeWindow(); };
        saveRelaunchButton_.onClick = [this] {
            if (!save()) return;
            closeWindow();
            if (relaunch_) relaunch_();
        };
        addAndMakeVisible(cancelButton_);
        addAndMakeVisible(saveButton_);
        addAndMakeVisible(saveRelaunchButton_);

        loadFromTarget();
        setSize(520, rowsBottom() + 78);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);

        auto place = [&](juce::Component& label, juce::Component& editor) {
            auto row = area.removeFromTop(kRowH);
            label.setBounds(row.removeFromLeft(kLabelW).reduced(0, 1));
            row.removeFromLeft(8);
            editor.setBounds(row.reduced(0, 1));
            area.removeFromTop(kRowGap);
        };

        for (auto const& item : items_) {
            if (item.editor == nullptr) {           // section header
                area.removeFromTop(kSectionGap);
                item.label->setBounds(area.removeFromTop(kRowH - 4));
                area.removeFromTop(2);
                continue;
            }
            place(*item.label, *item.editor);
            if (item.editor == &targetBox_) {
                pathLabel_.setBounds(area.removeFromTop(18)
                                         .withTrimmedLeft(kLabelW + 8));
                area.removeFromTop(kRowGap);
            }
            if (item.editor == &firstInChannel_) {
                inputHint_.setBounds(area.removeFromTop(18));
                area.removeFromTop(kRowGap);
            }
        }

        auto bottom = getLocalBounds().reduced(14, 12).removeFromBottom(58);
        noteLabel_.setBounds(bottom.removeFromTop(30));
        auto buttons = bottom.removeFromTop(26);
        saveRelaunchButton_.setBounds(buttons.removeFromRight(140));
        buttons.removeFromRight(8);
        saveButton_.setBounds(buttons.removeFromRight(80));
        buttons.removeFromRight(8);
        cancelButton_.setBounds(buttons.removeFromRight(80));
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

    int rowsBottom() const {
        int y = 12;
        for (auto const& item : items_) {
            if (item.editor == nullptr) { y += kSectionGap + kRowH - 2; continue; }
            y += kRowH + kRowGap;
            if (item.editor == &targetBox_ || item.editor == &firstInChannel_)
                y += 18 + kRowGap;
        }
        return y;
    }

    bool targetIsProject() const { return targetBox_.getSelectedId() == 1; }

    juce::File targetFile() const {
        if (targetIsProject())
            return juce::File(juce::String(projectDir_)).getChildFile("tzpl-config");
        return juce::File(juce::String(userConfigFile()));
    }

    // Show what the chosen file will actually produce at launch: defaults,
    // then the user file, then the project file when that is the target.
    void loadFromTarget() {
        AppConfig cfg;
        if (std::string user = userConfigFile(); !user.empty())
            loadConfigFile(user, cfg);
        if (targetIsProject())
            loadConfigFile(targetFile().getFullPathName().toStdString(), cfg);

        auto selectOrAdd = [](juce::ComboBox& box, juce::String const& name,
                              int defaultId) {
            if (name.isEmpty()) { box.setSelectedId(defaultId, juce::dontSendNotification); return; }
            for (int i = 0; i < box.getNumItems(); ++i) {
                if (box.getItemText(i) == name) {
                    box.setSelectedId(box.getItemId(i), juce::dontSendNotification);
                    return;
                }
            }
            // Configured device that is not plugged in right now: keep it
            // rather than silently rewriting the file to something else.
            int id = box.getNumItems() + 1;
            box.addItem(name + "  (not connected)", id);
            box.setSelectedId(id, juce::dontSendNotification);
        };

        juce::String outDev(cfg.deviceName);
        selectOrAdd(deviceBox_, outDev == "default" ? juce::String() : outDev, 1);
        juce::String inDev(cfg.inputDeviceName);
        selectOrAdd(inDeviceBox_, inDev == "default" ? juce::String() : inDev, 1);

        channels_.setText(juce::String(cfg.channels), false);
        firstChannel_.setText(juce::String(cfg.firstChannel), false);
        inChannels_.setText(juce::String(cfg.inputChannels), false);
        firstInChannel_.setText(juce::String(cfg.firstInputChannel), false);
        sampleRateBox_.setText(juce::String((int)cfg.sampleRate),
                               juce::dontSendNotification);
        bufferBox_.setText(juce::String(cfg.bufferFrames),
                           juce::dontSendNotification);
        silos_.setText(juce::String(cfg.numSilos), false);
        tempoClocks_.setText(juce::String(cfg.numTempoClocks), false);
        oscPort_.setText(juce::String(cfg.oscPort), false);
        natsUrl_.setText(juce::String(cfg.natsUrl), false);
        engineName_.setText(juce::String(cfg.engineName), false);

        pathLabel_.setText(targetFile().getFullPathName(),
                           juce::dontSendNotification);
    }

    static int intOf(juce::TextEditor const& e, int fallback, int lo) {
        auto t = e.getText().trim();
        return t.isEmpty() ? fallback : juce::jmax(lo, t.getIntValue());
    }

    bool save() {
        AppConfig cfg;
        // Start from the file being written so unknown-to-the-form state
        // (a key added by hand) round-trips untouched.
        std::string path = targetFile().getFullPathName().toStdString();
        loadConfigFile(path, cfg);

        cfg.deviceName = deviceBox_.getSelectedId() == 1
            ? "default" : deviceBox_.getText().upToFirstOccurrenceOf("  (not connected)", false, false).toStdString();
        cfg.inputDeviceName = inDeviceBox_.getSelectedId() == 1
            ? "" : inDeviceBox_.getText().upToFirstOccurrenceOf("  (not connected)", false, false).toStdString();
        cfg.channels = intOf(channels_, cfg.channels, 1);
        cfg.firstChannel = intOf(firstChannel_, cfg.firstChannel, 0);
        cfg.inputChannels = intOf(inChannels_, cfg.inputChannels, 0);
        cfg.firstInputChannel = intOf(firstInChannel_, cfg.firstInputChannel, 0);
        double sr = sampleRateBox_.getText().trim().getDoubleValue();
        if (sr >= 8000.0) cfg.sampleRate = sr;
        int bf = bufferBox_.getText().trim().getIntValue();
        if (bf >= 16) cfg.bufferFrames = bf;
        cfg.numSilos = intOf(silos_, cfg.numSilos, 1);
        cfg.numTempoClocks = intOf(tempoClocks_, cfg.numTempoClocks, 1);
        cfg.oscPort = intOf(oscPort_, cfg.oscPort, 0);
        cfg.natsUrl = natsUrl_.getText().trim().toStdString();
        cfg.engineName = engineName_.getText().trim().toStdString();

        std::string err;
        if (!saveConfigFile(path, cfg, &err)) {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon, "Could not save settings",
                juce::String(err), this);
            return false;
        }
        if (log_) log_("settings saved: " + juce::String(path)
                       + " (applies on relaunch)");
        return true;
    }

    void closeWindow() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    }

    std::string projectDir_;
    std::function<void(juce::String const&)> log_;
    std::function<void()> relaunch_;
    std::vector<Row> items_;

    juce::Label targetLabel_, pathLabel_, noteLabel_, inputHint_;
    juce::ComboBox targetBox_;
    juce::Label outSection_, inSection_, engineSection_, netSection_;
    juce::Label deviceLabel_, channelsLabel_, firstChannelLabel_;
    juce::ComboBox deviceBox_;
    juce::TextEditor channels_, firstChannel_;
    juce::Label inDeviceLabel_, inChannelsLabel_, firstInChannelLabel_;
    juce::ComboBox inDeviceBox_;
    juce::TextEditor inChannels_, firstInChannel_;
    juce::Label sampleRateLabel_, bufferLabel_, silosLabel_, clocksLabel_;
    juce::ComboBox sampleRateBox_, bufferBox_;
    juce::TextEditor silos_, tempoClocks_;
    juce::Label oscLabel_, natsLabel_, engineNameLabel_;
    juce::TextEditor oscPort_, natsUrl_, engineName_;
    juce::TextButton cancelButton_{"Cancel"}, saveButton_{"Save"},
                     saveRelaunchButton_{"Save & Relaunch"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};

juce::Component::SafePointer<juce::DialogWindow> gWindow;

}  // namespace

void showEngineSettings(std::string const& projectDir,
                        std::function<void(juce::String const&)> log,
                        std::function<void()> relaunch) {
    if (gWindow != nullptr) {
        gWindow->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(new SettingsComponent(projectDir, std::move(log),
                                             std::move(relaunch)));
    o.dialogTitle = "Engine Settings";
    o.dialogBackgroundColour = juce::LookAndFeel::getDefaultLookAndFeel()
        .findColour(juce::ResizableWindow::backgroundColourId);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    gWindow = o.launchAsync();  // deletes itself when closed
}

void closeEngineSettings() {
    // launchAsync() entered a modal state with deleteWhenDismissed set, so
    // leaving it is what frees the window.
    if (gWindow != nullptr) gWindow->exitModalState(0);
}

}  // namespace tzplapp
