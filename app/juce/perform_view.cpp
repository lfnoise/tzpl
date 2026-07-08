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
//  perform_view.cpp
//  app (JUCE)
//

#include "perform_view.hpp"
#include "tzpl_app_context.hpp"

namespace tzplapp {

PerformView::PerformView(bridge::AppContext& appCtx,
                         ControlsDispatcher& dispatcher,
                         std::function<std::vector<std::string>()> panels,
                         std::function<void()> onExit)
    : appCtx_(appCtx), dispatcher_(dispatcher), panels_(std::move(panels)),
      onExit_(std::move(onExit)) {
    setWantsKeyboardFocus(true);
    exitButton_.onClick = [this] { if (onExit_) onExit_(); };
    addAndMakeVisible(exitButton_);
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    titleLabel_.setText("esc exits -- widget keys are live",
                        juce::dontSendNotification);
    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(tabStrip_);
    addAndMakeVisible(content_);
}

void PerformView::refreshPanels() {
    panelNames_ = panels_ ? panels_() : std::vector<std::string>{};
    tabs_.clear();
    for (int i = 0; i < (int)panelNames_.size(); ++i) {
        auto b = std::make_unique<juce::TextButton>(panelNames_[(size_t)i]);
        b->onClick = [this, i] { showPanel(i); };
        tabStrip_.addAndMakeVisible(*b);
        tabs_.push_back(std::move(b));
    }
    if (selected_ >= (int)panelNames_.size()) selected_ = 0;
    showPanel(selected_);
    resized();
    grabKeyboardFocus();
}

void PerformView::showPanel(int index) {
    canvas_.reset();
    selected_ = index;
    if (index < 0 || index >= (int)panelNames_.size()) return;
    canvas_ = std::make_unique<PanelCanvas>(*appCtx_.uiState,
                                            panelNames_[(size_t)index],
                                            dispatcher_);
    content_.addAndMakeVisible(*canvas_);
    for (int i = 0; i < (int)tabs_.size(); ++i)
        tabs_[(size_t)i]->setColour(
            juce::TextButton::buttonColourId,
            i == selected_ ? juce::Colour(0xff4a70a0) : juce::Colour(0x30ffffff));
    resized();
}

void PerformView::resized() {
    auto r = getLocalBounds();
    auto bar = r.removeFromTop(kBarH).reduced(4, 4);
    exitButton_.setBounds(bar.removeFromLeft(100));
    bar.removeFromLeft(8);
    titleLabel_.setBounds(bar);

    tabStrip_.setBounds(r.removeFromTop(kTabH));
    int x = 4;
    for (auto& b : tabs_) {
        int tw = juce::jmax(60, b->getBestWidthForHeight(kTabH - 4));
        b->setBounds(x, 2, tw, kTabH - 4);
        x += tw + 4;
    }

    content_.setBounds(r);
    if (canvas_) {
        // Scale the canvas up: lay it out at 1/scale of the content and let
        // the transform enlarge it (which scales painting and mouse coords).
        canvas_->setTransform({});
        canvas_->setBounds(0, 0, juce::roundToInt(content_.getWidth() / kScale),
                           juce::roundToInt(content_.getHeight() / kScale));
        canvas_->setTransform(juce::AffineTransform::scale(kScale));
    }
}

void PerformView::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
    if (panelNames_.empty()) {
        g.setColour(juce::Colour(0xff808080));
        g.setFont(juce::FontOptions(15.0f));
        g.drawText("No panels to perform. Add a Panel cell and run code that "
                   "creates widgets.",
                   getLocalBounds(), juce::Justification::centred);
    }
}

bool PerformView::keyPressed(juce::KeyPress const& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onExit_) onExit_();
        return true;
    }
    return false;
}

}
