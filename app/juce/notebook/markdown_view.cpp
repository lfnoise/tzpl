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
//  markdown_view.cpp
//  app (JUCE)
//

#include "markdown_view.hpp"
#include "../tzpl_fonts.hpp"
#include "../../vendor/md4c/md4c.h"

#include <cmath>

namespace tzplapp {

using juce::String;

// ---------------------------------------------------------------------------
// Parsed model
// ---------------------------------------------------------------------------

namespace {
enum : int { kBold = 1, kItalic = 2, kCode = 4, kStrike = 8 };
}

struct MarkdownView::Model {
    struct Run { String text; int style = 0; };
    using Runs = std::vector<Run>;

    struct Block {
        enum class Kind { heading, para, code, table, rule };
        Kind kind = Kind::para;
        int level = 0;            // heading: 1..6; para: list nesting depth
        bool quoted = false;
        Runs runs;                // heading / para
        String code;              // code block (raw, with newlines)
        std::vector<std::vector<Runs>> rows;   // table: rows -> cells -> runs
    };

    std::vector<Block> blocks;
    String source;
};

// md4c callback state. Tight lists give text directly inside an LI (no
// MD_BLOCK_P), so text() lazily opens a paragraph when none is current.
namespace {
struct ParseCtx {
    MarkdownView::Model* m = nullptr;
    int style = 0;
    int quoteDepth = 0;
    struct ListCtx { bool ordered; int index; };
    std::vector<ListCtx> lists;
    String pendingBullet;
    MarkdownView::Model::Block* cur = nullptr;
    std::vector<MarkdownView::Model::Runs>* curRow = nullptr;
    MarkdownView::Model::Runs* curCell = nullptr;

    MarkdownView::Model::Block& newBlock(MarkdownView::Model::Block::Kind k) {
        auto& b = m->blocks.emplace_back();
        b.kind = k;
        b.quoted = quoteDepth > 0;
        return b;
    }

    MarkdownView::Model::Block& openPara() {
        auto& b = newBlock(MarkdownView::Model::Block::Kind::para);
        b.level = (int)lists.size();
        if (pendingBullet.isNotEmpty()) {
            b.runs.push_back({ pendingBullet, 0 });
            pendingBullet.clear();
        }
        cur = &b;
        return b;
    }
};

int enterBlockCB(MD_BLOCKTYPE type, void* detail, void* ud) {
    auto& c = *static_cast<ParseCtx*>(ud);
    using Kind = MarkdownView::Model::Block::Kind;
    switch (type) {
        case MD_BLOCK_QUOTE: c.quoteDepth++; break;
        case MD_BLOCK_UL: c.lists.push_back({ false, 0 }); break;
        case MD_BLOCK_OL: {
            auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            c.lists.push_back({ true, (int)d->start });
            break;
        }
        case MD_BLOCK_LI: {
            auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            if (d->is_task)
                c.pendingBullet = String::fromUTF8(
                    d->task_mark == ' ' ? "☐ " : "☑ ");
            else if (!c.lists.empty() && c.lists.back().ordered)
                c.pendingBullet = String(c.lists.back().index++) + ". ";
            else
                c.pendingBullet = String::fromUTF8("• ");
            break;
        }
        case MD_BLOCK_HR: c.newBlock(Kind::rule); break;
        case MD_BLOCK_H: {
            auto& b = c.newBlock(Kind::heading);
            b.level = (int)static_cast<MD_BLOCK_H_DETAIL*>(detail)->level;
            c.cur = &b;
            break;
        }
        case MD_BLOCK_P: c.openPara(); break;
        case MD_BLOCK_CODE:
        case MD_BLOCK_HTML: c.cur = &c.newBlock(Kind::code); break;
        case MD_BLOCK_TABLE: c.newBlock(Kind::table); break;
        case MD_BLOCK_TR:
            if (!c.m->blocks.empty()
                && c.m->blocks.back().kind == Kind::table) {
                c.m->blocks.back().rows.emplace_back();
                c.curRow = &c.m->blocks.back().rows.back();
            }
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            if (c.curRow) {
                c.curRow->emplace_back();
                c.curCell = &c.curRow->back();
            }
            break;
        default: break;
    }
    return 0;
}

int leaveBlockCB(MD_BLOCKTYPE type, void*, void* ud) {
    auto& c = *static_cast<ParseCtx*>(ud);
    switch (type) {
        case MD_BLOCK_QUOTE: c.quoteDepth--; break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL: if (!c.lists.empty()) c.lists.pop_back(); break;
        case MD_BLOCK_LI: c.pendingBullet.clear(); break;
        case MD_BLOCK_H:
        case MD_BLOCK_P:
        case MD_BLOCK_CODE:
        case MD_BLOCK_HTML: c.cur = nullptr; break;
        case MD_BLOCK_TABLE: c.curRow = nullptr; break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: c.curCell = nullptr; break;
        default: break;
    }
    return 0;
}

int enterSpanCB(MD_SPANTYPE type, void*, void* ud) {
    auto& c = *static_cast<ParseCtx*>(ud);
    switch (type) {
        case MD_SPAN_STRONG: c.style |= kBold; break;
        case MD_SPAN_EM: c.style |= kItalic; break;
        case MD_SPAN_CODE: c.style |= kCode; break;
        case MD_SPAN_DEL: c.style |= kStrike; break;
        default: break;
    }
    return 0;
}

int leaveSpanCB(MD_SPANTYPE type, void*, void* ud) {
    auto& c = *static_cast<ParseCtx*>(ud);
    switch (type) {
        case MD_SPAN_STRONG: c.style &= ~kBold; break;
        case MD_SPAN_EM: c.style &= ~kItalic; break;
        case MD_SPAN_CODE: c.style &= ~kCode; break;
        case MD_SPAN_DEL: c.style &= ~kStrike; break;
        default: break;
    }
    return 0;
}

int textCB(MD_TEXTTYPE type, MD_CHAR const* chars, MD_SIZE size, void* ud) {
    auto& c = *static_cast<ParseCtx*>(ud);
    using Kind = MarkdownView::Model::Block::Kind;

    if (type == MD_TEXT_NULLCHAR) return 0;

    String t;
    if (type == MD_TEXT_BR) t = "\n";
    else if (type == MD_TEXT_SOFTBR) t = " ";
    else t = String::fromUTF8(chars, (int)size);

    if (c.curCell) {
        c.curCell->push_back({ t, c.style });
        return 0;
    }
    if (c.cur && c.cur->kind == Kind::code) {
        c.cur->code += t;
        return 0;
    }
    if (!c.cur) c.openPara();   // tight list item / stray text
    c.cur->runs.push_back({ t, c.style });
    return 0;
}
}

// ---------------------------------------------------------------------------
// Layout (one width; heights are pure functions of the model + width)
// ---------------------------------------------------------------------------

struct MarkdownView::Layout {
    int width = -1;
    float height = 0;

    struct Item {
        Model::Block const* b = nullptr;
        juce::TextLayout tl;                  // heading / para / code
        juce::Rectangle<float> box;           // where it draws
        std::vector<float> colW;              // table geometry
        std::vector<float> rowH;
        std::vector<std::vector<juce::TextLayout>> cellTls;
    };
    std::vector<Item> items;
};

MarkdownView::MarkdownView() {
    model_ = std::make_unique<Model>();
    setWantsKeyboardFocus(true);   // click focuses -> cell gets selected
}

MarkdownView::~MarkdownView() = default;

void MarkdownView::setMarkdown(String const& text) {
    auto model = std::make_unique<Model>();
    model->source = text;

    ParseCtx ctx;
    ctx.m = model.get();

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = enterBlockCB;
    parser.leave_block = leaveBlockCB;
    parser.enter_span = enterSpanCB;
    parser.leave_span = leaveSpanCB;
    parser.text = textCB;

    auto utf8 = text.toRawUTF8();
    md_parse(utf8, (MD_SIZE)strlen(utf8), &parser, &ctx);

    model_ = std::move(model);
    layout_.reset();
    repaint();
}

void MarkdownView::setFontSize(float px) {
    if (juce::approximatelyEqual(fontSize_, px)) return;
    fontSize_ = px;
    layout_.reset();
    repaint();
}

void MarkdownView::lookAndFeelChanged() {
    layout_.reset();   // colours are baked into the cached TextLayouts
    repaint();
}

String MarkdownView::plainText() const {
    String out;
    for (auto const& b : model_->blocks) {
        for (auto const& r : b.runs) out += r.text;
        out += b.code;
        for (auto const& row : b.rows)
            for (auto const& cell : row)
                for (auto const& r : cell) out += r.text;
    }
    return out;
}

namespace {

juce::Font runFont(int style, float px, bool forceBold = false) {
    if (style & kCode)
        return juce::Font(monoFont(px * 0.92f));
    int flags = juce::Font::plain;
    if ((style & kBold) || forceBold) flags |= juce::Font::bold;
    if (style & kItalic) flags |= juce::Font::italic;
    return juce::Font(juce::FontOptions(
        juce::Font::getDefaultSansSerifFontName(), px, flags));
}

juce::AttributedString runsToAttr(std::vector<MarkdownView::Model::Run> const& runs,
                                  float px, juce::Colour colour,
                                  bool forceBold = false) {
    juce::AttributedString as;
    as.setWordWrap(juce::AttributedString::byWord);
    for (auto const& r : runs)
        as.append(r.text, runFont(r.style, px, forceBold), colour);
    return as;
}

float layoutHeight(juce::TextLayout const& tl) {
    return tl.getHeight();
}

}

void MarkdownView::ensureLayout(int width) const {
    if (layout_ && layout_->width == width) return;
    auto lay = std::make_unique<Layout>();
    lay->width = width;

    auto colour = findColour(juce::TextEditor::textColourId);
    float const fs = fontSize_;
    float const padV = 4.0f;
    float const paraGap = fs * 0.55f;
    float y = padV;
    bool first = true;

    using Kind = Model::Block::Kind;
    for (auto const& b : model_->blocks) {
        auto& item = lay->items.emplace_back();
        item.b = &b;

        float indent = b.level * fs * 1.3f + (b.quoted ? fs : 0.0f);
        float x = 2.0f + indent;
        float avail = juce::jmax(40.0f, (float)width - x - 2.0f);
        if (!first) y += paraGap;

        switch (b.kind) {
            case Kind::heading: {
                float scale = b.level <= 1 ? 1.6f
                            : b.level == 2 ? 1.35f
                            : b.level == 3 ? 1.15f : 1.0f;
                if (!first) y += fs * 0.5f;   // extra air above headings
                auto as = runsToAttr(b.runs, fs * scale, colour, true);
                item.tl.createLayout(as, avail);
                item.box = { x, y, avail, layoutHeight(item.tl) };
                y += item.box.getHeight();
                break;
            }
            case Kind::para: {
                auto as = runsToAttr(b.runs, fs, colour);
                item.tl.createLayout(as, avail);
                item.box = { x, y, avail, layoutHeight(item.tl) };
                y += item.box.getHeight();
                break;
            }
            case Kind::code: {
                juce::AttributedString as;
                as.setWordWrap(juce::AttributedString::byWord);
                String code = b.code;
                while (code.endsWithChar('\n')) code = code.dropLastCharacters(1);
                as.append(code.isEmpty() ? " " : code,
                          juce::Font(monoFont(fs * 0.92f)), colour);
                float pad = fs * 0.55f;
                item.tl.createLayout(as, avail - 2 * pad);
                item.box = { x, y, avail,
                             layoutHeight(item.tl) + 2 * pad };
                y += item.box.getHeight();
                break;
            }
            case Kind::rule: {
                item.box = { x, y, avail, fs * 0.6f };
                y += item.box.getHeight();
                break;
            }
            case Kind::table: {
                size_t nCols = 0;
                for (auto const& row : b.rows)
                    nCols = std::max(nCols, row.size());
                if (nCols == 0) { item.box = { x, y, avail, 0.0f }; break; }

                float cellPad = fs * 0.45f;
                // Natural (unwrapped) column widths.
                std::vector<float> nat(nCols, 0.0f);
                for (size_t ri = 0; ri < b.rows.size(); ++ri) {
                    for (size_t ci = 0; ci < b.rows[ri].size(); ++ci) {
                        auto as = runsToAttr(b.rows[ri][ci], fs, colour,
                                             ri == 0);
                        juce::TextLayout tl;
                        tl.createLayout(as, 1.0e6f);
                        nat[ci] = std::max(nat[ci], tl.getWidth() + 2.0f);
                    }
                }
                float availCols = avail - (float)nCols * 2 * cellPad;
                float total = 0;
                for (float w : nat) total += w;
                item.colW = nat;
                if (total > availCols) {
                    // Cap the wide columns: find c with sum(min(nat,c)) fit.
                    float lo = 8.0f, hi = availCols;
                    for (int it = 0; it < 24; ++it) {
                        float c = (lo + hi) / 2, s = 0;
                        for (float w : nat) s += std::min(w, c);
                        (s > availCols ? hi : lo) = c;
                    }
                    for (auto& w : item.colW) w = std::min(w, lo);
                }

                // Lay cells out at their final widths.
                item.cellTls.resize(b.rows.size());
                item.rowH.assign(b.rows.size(), 0.0f);
                for (size_t ri = 0; ri < b.rows.size(); ++ri) {
                    item.cellTls[ri].resize(nCols);
                    for (size_t ci = 0; ci < b.rows[ri].size(); ++ci) {
                        auto as = runsToAttr(b.rows[ri][ci], fs, colour,
                                             ri == 0);
                        item.cellTls[ri][ci].createLayout(as, item.colW[ci]);
                        item.rowH[ri] = std::max(
                            item.rowH[ri],
                            layoutHeight(item.cellTls[ri][ci]));
                    }
                    item.rowH[ri] += cellPad;   // row breathing room
                }
                float th = 0;
                for (float rh : item.rowH) th += rh;
                item.box = { x, y, avail, th + cellPad };
                y += item.box.getHeight();
                break;
            }
        }
        first = false;
    }
    lay->height = y + padV;
    layout_ = std::move(lay);
}

int MarkdownView::heightForWidth(int width) const {
    if (model_->blocks.empty())
        return juce::roundToInt(fontSize_ * 2.8f);   // empty: keep clickable
    ensureLayout(width);
    return (int)std::ceil(layout_->height);
}

void MarkdownView::paint(juce::Graphics& g) {
    ensureLayout(getWidth());
    auto colour = findColour(juce::TextEditor::textColourId);
    float const fs = fontSize_;

    if (model_->blocks.empty()) {
        g.setColour(colour.withAlpha(0.35f));
        g.setFont(juce::Font(juce::FontOptions(
            juce::Font::getDefaultSansSerifFontName(), fs,
            juce::Font::italic)));
        g.drawText("(empty prose cell -- double-click to edit)",
                   getLocalBounds().reduced(4),
                   juce::Justification::centredLeft);
        return;
    }

    using Kind = Model::Block::Kind;
    for (auto const& item : layout_->items) {
        auto const& b = *item.b;
        auto box = item.box;

        if (b.quoted) {   // accent bar to the left of quoted blocks
            g.setColour(colour.withAlpha(0.3f));
            g.fillRect(box.getX() - fs * 0.7f, box.getY(), 3.0f,
                       box.getHeight());
        }

        switch (b.kind) {
            case Kind::heading:
            case Kind::para:
                item.tl.draw(g, box);
                break;
            case Kind::code: {
                g.setColour(colour.withAlpha(0.07f));
                g.fillRoundedRectangle(box, 4.0f);
                float pad = fs * 0.55f;
                item.tl.draw(g, box.reduced(pad));
                break;
            }
            case Kind::rule:
                g.setColour(colour.withAlpha(0.25f));
                g.fillRect(box.getX(), box.getCentreY(), box.getWidth(),
                           1.0f);
                break;
            case Kind::table: {
                float cellPad = fs * 0.45f;
                float ry = box.getY() + cellPad * 0.5f;
                for (size_t ri = 0; ri < item.cellTls.size(); ++ri) {
                    float cx = box.getX();
                    for (size_t ci = 0; ci < item.cellTls[ri].size(); ++ci) {
                        juce::Rectangle<float> cell(
                            cx + cellPad, ry,
                            item.colW[ci], item.rowH[ri]);
                        item.cellTls[ri][ci].draw(g, cell);
                        cx += item.colW[ci] + 2 * cellPad;
                    }
                    ry += item.rowH[ri];
                    // Separator: strong under the header, light elsewhere.
                    g.setColour(colour.withAlpha(ri == 0 ? 0.4f : 0.12f));
                    if (ri + 1 < item.cellTls.size())
                        g.fillRect(box.getX(), ry - cellPad * 0.25f,
                                   box.getWidth(), ri == 0 ? 1.5f : 1.0f);
                }
                break;
            }
        }
    }
}

}
