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
//  plugin_browser_window.cpp
//  app (JUCE)
//

#include "plugin_browser_window.hpp"
#include "plugin_tags.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_fonts.hpp"
#include <algorithm>
#include <format>
#include <map>
#include <vector>

namespace tzplapp {

using juce::String;

namespace {

char const* elemName(tzpl_ElemType e) {
    switch (e) {
        case tzpl_kI32: return "i32";
        case tzpl_kF32: return "f32";
        case tzpl_kI64: return "i64";
        case tzpl_kF64: return "f64";
    }
    return "?";
}

char const* rateName(tzpl_Rate r) {
    switch (r) {
        case tzpl_constRate: return "const";
        case tzpl_initRate:  return "init";
        case tzpl_resetRate: return "reset";
        case tzpl_eventRate: return "event";
        case tzpl_audioRate: return "audio";
    }
    return "?";
}

String typeText(tzpl_SignalType const& t) {
    return String(t.chans) + "ch " + elemName(t.elem) + " " + rateName(t.rate);
}

String numText(double v) {
    return String(std::format("{:g}", v));
}

// Cheap change detection between refreshes: names, section sizes and tags
// only (defs are immutable once registered; a superseding def keeps its
// name but this is a browser, not an auditor). Tags matter because they
// decide row visibility.
bool sameDefs(std::vector<engine::DefDesc> const& a,
              std::vector<engine::DefDesc> const& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name
            || a[i].ins.size() != b[i].ins.size()
            || a[i].outs.size() != b[i].outs.size()
            || a[i].controls.size() != b[i].controls.size()
            || a[i].buffers.size() != b[i].buffers.size()
            || a[i].tags != b[i].tags) return false;
    }
    return true;
}

bool sameFiles(std::vector<engine::PluginFile> const& a,
               std::vector<engine::PluginFile> const& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name) return false;
    return true;
}

}

// ---------------------------------------------------------------------------
// Content: filter box over [ two-section name list | details viewport ]
// ---------------------------------------------------------------------------

class PluginBrowserWindow::Content : public juce::Component,
                                     public juce::ListBoxModel,
                                     private juce::Timer {
public:
    explicit Content(bridge::AppContext& appCtx)
        : appCtx_(appCtx), list_("plugins", this)
    {
        filter_.setFont(monoFont(14.0f));
        filter_.setTextToShowWhenEmpty("Filter", juce::Colours::grey);
        filter_.onTextChange = [this] { applyFilter(); updateDetails(); };
        addAndMakeVisible(filter_);

        showHidden_.onClick = [this] { applyFilter(); };
        addAndMakeVisible(showHidden_);

        tagsButton_.onClick = [this] { showTagsMenu(); };
        addAndMakeVisible(tagsButton_);

        list_.setRowHeight(36);
        addAndMakeVisible(list_);

        loadButton_.onClick = [this] { loadSelected(); };
        addChildComponent(loadButton_);
        loadError_.setText("load failed", juce::dontSendNotification);
        loadError_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        loadError_.setFont(monoFont(12.0f));
        addChildComponent(loadError_);

        addTag_.setFont(monoFont(12.0f));
        addTag_.setTextToShowWhenEmpty(String::fromUTF8("add tag\xe2\x80\xa6"),
                                       juce::Colours::grey);
        addTag_.onReturnKey = [this] { commitTag(); };
        addChildComponent(addTag_);

        details_.onRemoveTag = [this](String const& tag) {
            if (selectedName_.empty()) return;
            store_.removeLocalTag(selectedName_, tag.toStdString());
            applyFilter();
            updateDetails();
        };

        viewport_.setViewedComponent(&details_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);

        refresh();
        startTimerHz(1);
    }

    void refresh() {
        bool haveEngine = appCtx_.engine != nullptr;
        std::vector<engine::DefDesc> fresh;
        if (haveEngine) engine::listDefDescs(appCtx_.engine, fresh);

        // Plugins on disk not registered under any loaded def's name
        // (scanning needs no engine; listPluginFiles never dlopens).
        std::vector<engine::PluginFile> files;
        engine::listPluginFiles(appCtx_.pluginSearchPaths, files);
        std::vector<engine::PluginFile> freshAvail;
        for (auto& f : files) {
            bool loaded = false;
            for (auto const& d : fresh)
                if (d.name == f.name) { loaded = true; break; }
            if (!loaded) freshAvail.push_back(std::move(f));
        }

        if (haveEngine == engineSeen_ && sameDefs(fresh, defs_)
            && sameFiles(freshAvail, avail_)) return;
        engineSeen_ = haveEngine;
        defs_ = std::move(fresh);
        avail_ = std::move(freshAvail);
        applyFilter();
        updateDetails();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(getLookAndFeel().findColour(
            juce::ResizableWindow::backgroundColourId));
    }

    void resized() override {
        auto r = getLocalBounds().reduced(8);
        auto filterRow = r.removeFromTop(24);
        showHidden_.setBounds(filterRow.removeFromRight(160));
        filterRow.removeFromRight(4);
        tagsButton_.setBounds(filterRow.removeFromRight(70));
        filterRow.removeFromRight(8);
        filter_.setBounds(filterRow);
        r.removeFromTop(8);
        list_.setBounds(r.removeFromLeft(r.getWidth() * 2 / 5));
        r.removeFromLeft(8);
        if (loadButton_.isVisible() || loadError_.isVisible()
            || addTag_.isVisible()) {
            auto top = r.removeFromTop(24);
            if (loadButton_.isVisible()) {
                loadButton_.setBounds(top.removeFromLeft(80));
                top.removeFromLeft(8);
            }
            if (addTag_.isVisible()) {
                addTag_.setBounds(top.removeFromRight(140));
                top.removeFromRight(8);
            }
            loadError_.setBounds(top);
            r.removeFromTop(8);
        }
        viewport_.setBounds(r);
        layoutDetails();
    }

    // One flat row model over both sections. Header rows are painted but
    // never selectable.
    struct Row {
        enum Kind { kLoadedHeader, kLoaded, kAvailHeader, kAvail };
        Kind kind;
        int index;  // into defs_ (kLoaded) or avail_ (kAvail); -1 for headers
        bool hidden = false;  // filtered out by tag rules, shown via "show hidden"
    };

    static bool isHeader(Row const& r) {
        return r.kind == Row::kLoadedHeader || r.kind == Row::kAvailHeader;
    }

    // ListBoxModel
    int getNumRows() override { return (int)rows_.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                          bool selected) override {
        if (row < 0 || row >= (int)rows_.size()) return;
        auto const& r = rows_[(std::size_t)row];
        auto text = getLookAndFeel().findColour(juce::TextEditor::textColourId);
        if (isHeader(r)) {
            g.setColour(text.withAlpha(0.45f));
            g.setFont(monoFont(11.0f));
            g.drawText(r.kind == Row::kLoadedHeader ? "Loaded" : "Available",
                       8, 0, w - 12, h - 4,
                       juce::Justification::bottomLeft, true);
            return;
        }
        if (selected)
            g.fillAll(getLookAndFeel()
                          .findColour(juce::TextEditor::highlightColourId));
        float hid = r.hidden ? 0.45f : 1.0f;  // tag-filtered rows extra-dim
        if (r.kind == Row::kAvail) {
            // Counts are unknown without introspecting the file.
            g.setColour(text.withAlpha(0.7f).withMultipliedAlpha(hid));
            g.setFont(monoFont(14.0f));
            g.drawText(String(avail_[(std::size_t)r.index].name), 8, 0,
                       w - 12, h, juce::Justification::centredLeft, true);
            return;
        }
        auto const& d = defs_[(std::size_t)r.index];
        g.setColour(text.withMultipliedAlpha(hid));
        g.setFont(monoFont(14.0f));
        g.drawText(String(d.name), 8, 3, w - 12, 16,
                   juce::Justification::centredLeft, true);
        static String const dot = String::fromUTF8(" \xc2\xb7 ");
        String counts = String((int)d.ins.size()) + " in" + dot
                      + String((int)d.outs.size()) + " out" + dot
                      + String((int)d.controls.size()) + " ctl" + dot
                      + String((int)d.buffers.size()) + " buf";
        g.setColour(text.withAlpha(0.55f).withMultipliedAlpha(hid));
        g.setFont(monoFont(11.0f));
        g.drawText(counts, 8, h - 16, w - 12, 13,
                   juce::Justification::centredLeft, true);
    }

    juce::String getTooltipForRow(int row) override {
        if (row >= 0 && row < (int)rows_.size()) {
            auto const& r = rows_[(std::size_t)row];
            if (r.kind == Row::kAvail)
                return String(avail_[(std::size_t)r.index].path);
        }
        return {};
    }

    void selectedRowsChanged(int lastRowSelected) override {
        if (updatingList_) return;
        if (lastRowSelected >= 0 && lastRowSelected < (int)rows_.size()) {
            auto const& r = rows_[(std::size_t)lastRowSelected];
            if (isHeader(r)) { reapplySelection(); return; }
            loadFailed_ = false;
            if (r.kind == Row::kLoaded) {
                selectedIsAvail_ = false;
                selectedName_ = defs_[(std::size_t)r.index].name;
                selectedPath_.clear();
            } else {
                selectedIsAvail_ = true;
                selectedName_ = avail_[(std::size_t)r.index].name;
                selectedPath_ = avail_[(std::size_t)r.index].path;
            }
        } else {
            selectedName_.clear();
            selectedPath_.clear();
            selectedIsAvail_ = false;
            loadFailed_ = false;
        }
        updateDetails();
    }

private:
    void timerCallback() override {
        if (isShowing()) refresh();
    }

    // Rebuild the flattened row list from the filter (text + tag rules)
    // and re-select the remembered plugin.
    void applyFilter() {
        rows_.clear();
        String needle = filter_.getText().trim();
        auto matches = [&](std::string const& n) {
            return needle.isEmpty() || String(n).containsIgnoreCase(needle);
        };
        bool showHidden = showHidden_.getToggleState();
        int hiddenCount = 0;
        rows_.push_back({ Row::kLoadedHeader, -1 });
        for (int i = 0; i < (int)defs_.size(); ++i) {
            auto const& d = defs_[(std::size_t)i];
            if (!matches(d.name)) continue;
            bool hid = store_.shouldHide(d.name, d.tags);
            if (hid) { ++hiddenCount; if (!showHidden) continue; }
            rows_.push_back({ Row::kLoaded, i, hid });
        }
        std::vector<Row> availRows;
        for (int i = 0; i < (int)avail_.size(); ++i) {
            auto const& f = avail_[(std::size_t)i];
            if (!matches(f.name)) continue;
            bool hid = store_.shouldHide(f.name, availEmbedded(f));
            if (hid) { ++hiddenCount; if (!showHidden) continue; }
            availRows.push_back({ Row::kAvail, i, hid });
        }
        if (!availRows.empty()) {
            rows_.push_back({ Row::kAvailHeader, -1 });
            rows_.insert(rows_.end(), availRows.begin(), availRows.end());
        }
        showHidden_.setButtonText("show hidden (" + String(hiddenCount) + ")");

        updatingList_ = true;
        list_.updateContent();
        updatingList_ = false;
        reapplySelection();
    }

    // The "tags..." menu: one submenu per tag with a tri-state filter
    // (Show / Hide / Don't care). Hide wins; any Show tag makes the show
    // set a whitelist (see PluginTagStore).
    void showTagsMenu() {
        using TagFilter = PluginTagStore::TagFilter;
        // Tag universe: effective tags of everything listed, plus tags with
        // a filter state in the store (so a filtered tag with no current
        // plugins can still be reset). std::map keeps it sorted.
        std::map<std::string, int> counts;
        for (auto const& d : defs_)
            for (auto const& t : store_.effectiveTags(d.name, d.tags))
                ++counts[t];
        for (auto const& f : avail_)
            for (auto const& t : store_.effectiveTags(f.name, availEmbedded(f)))
                ++counts[t];
        for (auto const& t : store_.hiddenTags()) counts.emplace(t, 0);
        for (auto const& t : store_.shownTags()) counts.emplace(t, 0);

        juce::PopupMenu m;
        std::vector<std::string> tags;
        for (auto const& [tag, n] : counts) {
            // Item IDs: tagIndex * 3 + {1 Show, 2 Hide, 3 Don't care}.
            int base = (int)tags.size() * 3;
            tags.push_back(tag);
            TagFilter state = store_.filterFor(tag);
            // Parent items can't be ticked reliably across platforms, so
            // the state rides in the label.
            String label = String(tag) + " (" + String(n) + ")";
            if (state == TagFilter::Show) label += " [show]";
            else if (state == TagFilter::Hide) label += " [hide]";
            juce::PopupMenu sub;
            sub.addItem(base + 1, "Show", true, state == TagFilter::Show);
            sub.addItem(base + 2, "Hide", true, state == TagFilter::Hide);
            sub.addItem(base + 3, "Don't care", true,
                        state == TagFilter::DontCare);
            m.addSubMenu(label, sub);
        }
        if (tags.empty()) m.addItem(1, "no tags", false, false);

        m.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&tagsButton_),
            [safe = juce::Component::SafePointer<Content>(this),
             tags](int result) {
                if (safe == nullptr || result <= 0) return;
                int idx = (result - 1) / 3, ord = (result - 1) % 3;
                if (idx >= (int)tags.size()) return;
                auto state = ord == 0 ? TagFilter::Show
                           : ord == 1 ? TagFilter::Hide
                                      : TagFilter::DontCare;
                safe->store_.setFilter(tags[(std::size_t)idx], state);
                safe->applyFilter();
            });
    }

    // Re-select the remembered plugin (by name + section) without treating
    // it as a user selection change.
    void reapplySelection() {
        updatingList_ = true;
        int sel = -1;
        if (!selectedName_.empty()) {
            for (int i = 0; i < (int)rows_.size(); ++i) {
                auto const& r = rows_[(std::size_t)i];
                if (r.kind == Row::kLoaded && !selectedIsAvail_
                    && defs_[(std::size_t)r.index].name == selectedName_)
                    { sel = i; break; }
                if (r.kind == Row::kAvail && selectedIsAvail_
                    && avail_[(std::size_t)r.index].name == selectedName_)
                    { sel = i; break; }
            }
        }
        if (sel >= 0) list_.selectRow(sel);
        else list_.deselectAllRows();
        updatingList_ = false;
    }

    void updateDetails() {
        details_.rows.clear();
        bool availSelected = false;
        engine::DefDesc const* d = nullptr;
        if (selectedIsAvail_) {
            if (auto const* f = selectedAvail()) {
                availSelected = true;
                selectedPath_ = f->path;  // rescans can revise the path
                fetchFileDesc(f->path);
                if (cachedDescOk_) d = &cachedDesc_;
            }
        } else {
            d = selectedDef();
        }
        if (d || availSelected) {
            details_.rows.push_back({ String(d ? d->name : selectedName_),
                                      {}, {}, Details::kTitle });
            if (availSelected)
                details_.rows.push_back({ String(selectedPath_), {}, {},
                                          Details::kPath });
            // Effective tags as chips; user-local ones are removable.
            auto local = store_.localTags(selectedName_);
            auto eff = store_.effectiveTags(
                selectedName_, d ? d->tags : std::vector<std::string>{});
            if (!eff.empty()) {
                Details::Row tagRow { {}, {}, {}, Details::kTags };
                for (auto const& t : eff)
                    tagRow.chips.emplace_back(String(t),
                        std::find(local.begin(), local.end(), t) != local.end());
                details_.rows.push_back(std::move(tagRow));
            }
        }
        if (d) {
            addSection("Inlets", d->ins);
            addSection("Outlets", d->outs);
            if (!d->controls.empty()) {
                details_.rows.push_back({ "Controls", {}, {}, Details::kHeading });
                for (auto const& c : d->controls)
                    details_.rows.push_back({ String(c.name), typeText(c.type),
                        "lo " + numText(c.spec.lo) + "  hi " + numText(c.spec.hi)
                            + "  init " + numText(c.spec.init),
                        Details::kEntry });
            }
            if (!d->buffers.empty()) {
                details_.rows.push_back({ "Buffers", {}, {}, Details::kHeading });
                for (auto const& b : d->buffers)
                    details_.rows.push_back({ String(b.name), typeText(b.type),
                        "id " + String(b.bufID), Details::kEntry });
            }
        } else if (availSelected) {
            details_.rows.push_back({ "not a loadable plugin", {}, {},
                                      Details::kNote });
        } else if (!engineSeen_) {
            details_.rows.push_back({ "engine not running", {}, {}, Details::kNote });
        } else if (defs_.empty() && avail_.empty()) {
            details_.rows.push_back({ "no plugins loaded", {}, {}, Details::kNote });
        } else {
            details_.rows.push_back({ "no plugin selected", {}, {}, Details::kNote });
        }
        loadButton_.setVisible(availSelected && appCtx_.engine != nullptr);
        loadError_.setVisible(availSelected && loadFailed_);
        addTag_.setVisible(d != nullptr || availSelected);
        resized();  // strip widget visibility changes the details layout
        details_.repaint();
    }

    // Enter in the add-tag box: persist a user-local tag for the selected
    // plugin (name-keyed, so it applies loaded or not).
    void commitTag() {
        String tag = addTag_.getText().trim();
        addTag_.setText(String(), false);
        if (tag.isEmpty() || selectedName_.empty()) return;
        store_.addLocalTag(selectedName_, tag.toStdString());
        applyFilter();
        updateDetails();
    }

    // Load the selected available plugin into the engine; on success the
    // refresh moves it into the Loaded section (selection follows by name).
    void loadSelected() {
        if (!appCtx_.engine || !selectedIsAvail_ || selectedPath_.empty()) return;
        if (engine::loadOneDef(appCtx_.engine, selectedPath_.c_str())) {
            loadFailed_ = false;
            selectedIsAvail_ = false;
            selectedPath_.clear();
            refresh();
        } else {
            loadFailed_ = true;
            updateDetails();
        }
    }

    // One-entry introspection cache keyed by path (the engine caches by
    // path+mtime too; this just avoids re-calling on every data refresh).
    void fetchFileDesc(std::string const& path) {
        if (cachedDescPath_ == path) return;
        cachedDescPath_ = path;
        cachedDesc_ = {};
        cachedDescOk_ = engine::getPluginFileDesc(path.c_str(), cachedDesc_);
    }

    void addSection(char const* heading,
                    std::vector<engine::PortDesc> const& ports) {
        if (ports.empty()) return;
        details_.rows.push_back({ heading, {}, {}, Details::kHeading });
        for (auto const& p : ports)
            details_.rows.push_back({ String(p.name), typeText(p.type), {},
                                      Details::kEntry });
    }

    engine::DefDesc const* selectedDef() const {
        for (auto const& d : defs_)
            if (d.name == selectedName_) return &d;
        return nullptr;
    }

    engine::PluginFile const* selectedAvail() const {
        for (auto const& f : avail_)
            if (f.name == selectedName_) return &f;
        return nullptr;
    }

    // Embedded tags for an available plugin: only known once its file has
    // been introspected (no extra engine calls just for filtering -- name
    // patterns still apply either way).
    std::vector<std::string> const& availEmbedded(engine::PluginFile const& f) const {
        static std::vector<std::string> const none;
        if (cachedDescOk_ && cachedDescPath_ == f.path) return cachedDesc_.tags;
        return none;
    }

    void layoutDetails() {
        details_.setSize(viewport_.getMaximumVisibleWidth(),
                         juce::jmax(details_.preferredHeight(),
                                    viewport_.getHeight()));
    }

    // The right-hand pane: paints the selected def's sections; sized from
    // its row list so the viewport scrolls.
    struct Details : juce::Component {
        enum Kind { kTitle, kPath, kTags, kHeading, kEntry, kNote };
        struct Row {
            String name, type, extra;
            Kind kind;
            std::vector<std::pair<String, bool>> chips;  // kTags: tag, isLocal
        };
        std::vector<Row> rows;

        // Invoked when a local tag chip is clicked (to remove it).
        std::function<void(String const&)> onRemoveTag;

        static int rowHeight(Kind k) {
            switch (k) {
                case kTitle:   return 30;
                case kPath:    return 32;  // up to two wrapped lines
                case kTags:    return 26;
                case kHeading: return 26;
                default:       return 20;
            }
        }

        int preferredHeight() const {
            int h = 16;
            for (auto const& r : rows) h += rowHeight(r.kind);
            return h;
        }

        void paint(juce::Graphics& g) override {
            chipHits.clear();
            auto text = getLookAndFeel().findColour(
                juce::TextEditor::textColourId);
            auto dim = text.withAlpha(0.55f);
            int w = getWidth(), y = 8;
            for (auto const& r : rows) {
                int h = rowHeight(r.kind);
                switch (r.kind) {
                case kTitle:
                    g.setColour(text);
                    g.setFont(juce::FontOptions(monoFontName(), 16.0f,
                                                juce::Font::bold));
                    g.drawText(r.name, 8, y, w - 16, h,
                               juce::Justification::centredLeft, true);
                    break;
                case kHeading:
                    g.setColour(dim);
                    g.setFont(juce::FontOptions(monoFontName(), 12.0f,
                                                juce::Font::bold));
                    g.drawText(r.name, 8, y + 6, w - 16, h - 6,
                               juce::Justification::centredLeft, true);
                    break;
                case kPath:
                    // The file path; wraps onto a second line rather than
                    // losing the tail to an end-ellipsis.
                    g.setColour(dim);
                    g.setFont(monoFont(11.0f));
                    g.drawFittedText(r.name, 8, y, w - 16, h - 4,
                                     juce::Justification::topLeft, 2, 1.0f);
                    break;
                case kTags: {
                    // Tag chips: local ones in the accent colour with a
                    // trailing x, click to remove.
                    auto accent = getLookAndFeel().findColour(
                        juce::TextEditor::focusedOutlineColourId);
                    juce::Font chipFont { monoFont(11.0f) };
                    g.setFont(chipFont);
                    static String const x = String::fromUTF8(" \xc3\x97");
                    int cx = 16;
                    int const chipH = 18;
                    int cy = y + (h - chipH) / 2;
                    for (auto const& [tag, local] : r.chips) {
                        String label = local ? tag + x : tag;
                        int cw = juce::GlyphArrangement::getStringWidthInt(
                                     chipFont, label) + 12;
                        if (cx + cw > w - 8) break;  // out of room
                        juce::Rectangle<int> chip { cx, cy, cw, chipH };
                        g.setColour((local ? accent : dim).withAlpha(0.5f));
                        g.drawRoundedRectangle(chip.toFloat(), 4.0f, 1.0f);
                        g.setColour(local ? accent : dim);
                        g.drawText(label, chip, juce::Justification::centred,
                                   true);
                        if (local) chipHits.push_back({ chip, tag });
                        cx += cw + 6;
                    }
                    break;
                }
                case kNote:
                    g.setColour(dim);
                    g.setFont(monoFont(13.0f));
                    g.drawText(r.name, 8, y, w - 16, h,
                               juce::Justification::centredLeft, true);
                    break;
                case kEntry:
                    g.setFont(monoFont(13.0f));
                    g.setColour(text);
                    g.drawText(r.name, 16, y, 140, h,
                               juce::Justification::centredLeft, true);
                    g.setColour(dim);
                    g.drawText(r.type, 160, y, 130, h,
                               juce::Justification::centredLeft, true);
                    g.drawText(r.extra, 294, y, juce::jmax(10, w - 302), h,
                               juce::Justification::centredLeft, true);
                    break;
                }
                y += h;
            }
        }

        void mouseDown(juce::MouseEvent const& e) override {
            for (auto const& c : chipHits)
                if (c.area.contains(e.getPosition())) {
                    if (onRemoveTag) onRemoveTag(c.tag);
                    return;
                }
        }

        // Local tag chip rects from the last paint (only local chips are
        // clickable; a click after a resize but before the repaint would use
        // stale rects, harmlessly).
        struct ChipHit { juce::Rectangle<int> area; String tag; };
        std::vector<ChipHit> chipHits;
    };

    bridge::AppContext& appCtx_;
    PluginTagStore store_;
    juce::TextEditor filter_;
    juce::ToggleButton showHidden_ { "show hidden" };
    juce::TextButton tagsButton_ { "tags..." };
    juce::ListBox list_;
    juce::TextButton loadButton_ { "Load" };
    juce::Label loadError_;
    juce::TextEditor addTag_;
    juce::Viewport viewport_;
    Details details_;

    std::vector<engine::DefDesc> defs_;     // loaded (registered) defs
    std::vector<engine::PluginFile> avail_; // on disk, not loaded
    std::vector<Row> rows_;                 // flattened, filtered list rows
    std::string selectedName_;              // selection survives refresh + filter
    std::string selectedPath_;              // file path when selectedIsAvail_
    bool selectedIsAvail_ = false;
    bool engineSeen_ = false;
    bool updatingList_ = false;      // ignore programmatic selection changes
    bool loadFailed_ = false;        // last Load click failed (until reselect)

    std::string cachedDescPath_;     // one-entry getPluginFileDesc cache
    engine::DefDesc cachedDesc_;
    bool cachedDescOk_ = false;

    // Row tooltips (available-plugin paths) need a TooltipWindow to exist;
    // nothing else in the app creates one.
    juce::TooltipWindow tooltips_;
};

// ---------------------------------------------------------------------------
// PluginBrowserWindow
// ---------------------------------------------------------------------------

PluginBrowserWindow::PluginBrowserWindow(bridge::AppContext& appCtx)
    : juce::DocumentWindow("Plugins", juce::Colours::darkgrey,
                           juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    content_ = new Content(appCtx);
    setContentOwned(content_, false);
    centreWithSize(640, 420);
    setVisible(true);
}

void PluginBrowserWindow::closeButtonPressed() {
    setVisible(false);
    if (onClose) onClose();
}

void PluginBrowserWindow::refresh() {
    if (content_) content_->refresh();
}

}
