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
//  search_test.cpp
//  app
//
//  Headless tests for the editor's search core: the five-mode pattern
//  matcher (Find bar, Find in Files), the definition scanner, and the
//  import-following definition finder.
//

#include "text_search.hpp"
#include "symbol_scanner.hpp"
#include "definition_finder.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <print>

using namespace tzplapp;
namespace fs = std::filesystem;

static int gPassed = 0, gFailed = 0;

static void check(bool ok, char const* what) {
    if (ok) { ++gPassed; std::print("  ok   {}\n", what); }
    else    { ++gFailed; std::print("  FAIL {}\n", what); }
}

static std::vector<int> starts(std::vector<TextMatch> const& ms) {
    std::vector<int> out;
    for (auto const& m : ms) out.push_back(m.start);
    return out;
}

// ---------------------------------------------------------------------------

static void test_matcher() {
    std::print("Test: pattern matcher modes\n");
    std::wstring text = L"push push! pushed unpush Push\nrepush";
    //                     0    5     11     18     25    30

    check(starts(TextMatcher(L"push", MatchMode::contains, false).findAll(text))
              == std::vector<int>{0, 5, 11, 20, 25, 32},
          "contains (case-insensitive) finds every occurrence");
    check(starts(TextMatcher(L"push", MatchMode::contains, true).findAll(text))
              == std::vector<int>{0, 5, 11, 20, 32},
          "contains (case-sensitive) skips Push");
    check(starts(TextMatcher(L"push", MatchMode::matchesWord, false).findAll(text))
              == std::vector<int>{0, 25},
          "matches word: not push!, pushed, unpush, repush");
    check(starts(TextMatcher(L"push!", MatchMode::matchesWord, true).findAll(text))
              == std::vector<int>{5},
          "matches word: push! is its own identifier");
    check(starts(TextMatcher(L"push", MatchMode::startsWith, true).findAll(text))
              == std::vector<int>{0, 5, 11},
          "starts with: push, push!, pushed");
    check(starts(TextMatcher(L"push", MatchMode::endsWith, true).findAll(text))
              == std::vector<int>{0, 20, 32},
          "ends with: push, unpush, repush");

    TextMatcher re(L"p(u)sh(ed|!)", MatchMode::regex, true);
    check(re.valid(), "regex compiles");
    check(starts(re.findAll(text)) == std::vector<int>{5, 11},
          "regex: alternation matches push! and pushed");
    auto ms = re.findAll(text);
    check(ms.size() == 2
              && re.expandReplacement(text, ms[1], L"<$2:$1:$0>") == L"<ed:u:pushed>",
          "regex replacement expands $0, $1, $2");
    check(re.expandReplacement(text, ms[0], L"a$$b") == L"a$b",
          "regex replacement: $$ is a literal dollar");

    TextMatcher multi(L"^re", MatchMode::regex, true);
    check(starts(multi.findAll(text)) == std::vector<int>{30},
          "regex: ^ anchors at line starts");
    TextMatcher ci(L"PUSH$", MatchMode::regex, false);
    check(starts(ci.findAll(text)) == std::vector<int>{25, 32},
          "regex: icase and $ at line ends");
    TextMatcher bad(L"(unclosed", MatchMode::regex, true);
    check(!bad.valid() && bad.findAll(text).empty(),
          "invalid regex reports an error and matches nothing");
    TextMatcher empty(L"x*", MatchMode::regex, true);
    check(empty.findAll(L"aaa").empty(), "zero-length regex matches are dropped");
    check(TextMatcher(L"", MatchMode::contains, true).findAll(text).empty(),
          "empty term matches nothing");
    check(starts(TextMatcher(L"aa", MatchMode::contains, true).findAll(L"aaaa"))
              == std::vector<int>{0, 2},
          "literal matches do not overlap");
}

// ---------------------------------------------------------------------------

static Definition const* findDef(ScanResult const& r, wchar_t const* name,
                                 DefKind kind, int nth = 0) {
    for (auto const& d : r.defs)
        if (d.name == name && d.kind == kind && nth-- == 0) return &d;
    return nullptr;
}

static void test_scanner() {
    std::print("Test: definition scanner\n");
    std::wstring src =
        L"-- comment with fn fake(\n"
        L"import std.strings.*;\n"
        L"import music.tuning as tun;\n"
        L"import util.{helper, other as alias};\n"
        L"export synthdef.*;\n"
        L"let s = \"fn inString(\";\n"
        L"let raw = \"\"\"\n fn inRaw() {\n\"\"\";\n"
        L"private fn add(a Int, b Int) Int { a + b }\n"
        L"fn add(a Float, b Float) Float = a + b;\n"
        L"fn _hidden() Void {}\n"
        L"fn +(a Vec, b Vec) Vec { a }\n"
        L"fn identity<T>(x T) T { x }\n"
        L"struct Point { x Float; y Float; }\n"
        L"enum Option<T> { case Some(T); case None; }\n"
        L"type Natural = Int;\n"
        L"var `indent = 0;\n"
        L"const PI = 3.14;\n"
        L"let (p, q) = (1, 2);\n"
        L"fn outer(n Int) Int {\n"
        L"    let inner = n * 2;\n"
        L"    for i in 0..n { let sq = i * i; }\n"
        L"    match n { case .Some(v) => v; case x where x > 0 => x; case _ => 0; }\n"
        L"    xs map(fn(y Int) Int { y push!(1) })\n"
        L"}\n";
    ScanResult r = scanSource(src);

    check(!findDef(r, L"fake", DefKind::function)
              && !findDef(r, L"inString", DefKind::function)
              && !findDef(r, L"inRaw", DefKind::function),
          "comments and strings are skipped");
    auto* add0 = findDef(r, L"add", DefKind::function, 0);
    auto* add1 = findDef(r, L"add", DefKind::function, 1);
    check(add0 && add1 && add0->topLevel && add1->topLevel,
          "both overloads of add are top-level definitions");
    check(add0 && add0->isPrivate && add1 && !add1->isPrivate,
          "private keyword marks only the first overload");
    auto* hidden = findDef(r, L"_hidden", DefKind::function);
    check(hidden && hidden->isPrivate, "underscore prefix is private");
    check(findDef(r, L"+", DefKind::function) != nullptr, "operator overload");
    check(findDef(r, L"identity", DefKind::function) != nullptr, "template fn");
    auto* tparam = findDef(r, L"x", DefKind::parameter);
    check(tparam && !tparam->topLevel, "template fn parameter is scoped");
    check(findDef(r, L"Point", DefKind::structType) && !findDef(r, L"x", DefKind::constant),
          "struct name is a definition; its fields are not");
    auto* some = findDef(r, L"Some", DefKind::enumCase);
    check(findDef(r, L"Option", DefKind::enumType) && some && some->topLevel
              && findDef(r, L"None", DefKind::enumCase),
          "enum and its cases; cases visible where the enum is");
    check(findDef(r, L"Natural", DefKind::typeAlias) != nullptr, "type alias");
    check(findDef(r, L"`indent", DefKind::variable) != nullptr, "dynamic-scope var");
    check(findDef(r, L"PI", DefKind::constant) != nullptr, "const");
    check(findDef(r, L"p", DefKind::constant) && findDef(r, L"q", DefKind::constant),
          "tuple destructuring binds both names");

    auto* a0 = findDef(r, L"a", DefKind::parameter, 0);
    auto* a1 = findDef(r, L"a", DefKind::parameter, 1);
    check(a0 && a1 && !a0->topLevel, "parameters recorded per overload");
    check(a0 && add0 && a0->scopeStart > add0->start
              && a0->scopeEnd < (int)src.size()
              && definitionVisibleAt(*a0, a0->scopeStart + 1)
              && !definitionVisibleAt(*a0, a1->scopeStart),
          "block-body parameter scope is the body");
    check(a1 && definitionVisibleAt(*a1, a1->scopeStart + 1)
              && a1->scopeEnd < a0->scopeEnd + 80,
          "expression-body parameter scope ends at the semicolon");

    auto* inner = findDef(r, L"inner", DefKind::constant);
    auto* outer = findDef(r, L"outer", DefKind::function);
    check(inner && outer && !inner->topLevel && inner->scopeStart == inner->start
              && inner->scopeEnd > inner->start,
          "let inside a body is scoped from its own position to the block end");
    auto* i = findDef(r, L"i", DefKind::loopVariable);
    auto* sq = findDef(r, L"sq", DefKind::constant);
    check(i && sq && i->scopeStart < sq->start && sq->scopeEnd <= i->scopeEnd,
          "for variable scoped to the loop body; nested let inside it");
    auto* v = findDef(r, L"v", DefKind::binding);
    auto* mx = findDef(r, L"x", DefKind::binding);
    check(v && mx && !findDef(r, L"_", DefKind::binding)
              && !findDef(r, L"Some", DefKind::binding),
          "match bindings: v and x, not _ or the constructor");
    check(v && mx && v->scopeEnd <= mx->start,
          "a match binding ends at the next case");
    auto* y = findDef(r, L"y", DefKind::parameter);
    check(y && !y->topLevel && y->scopeStart > outer->start, "lambda parameter");
    check(!findDef(r, L"push!", DefKind::function), "a call is not a definition");

    check(r.imports.size() == 4, "four import statements");
    if (r.imports.size() == 4) {
        check(r.imports[0].wildcard && r.imports[0].path.size() == 2
                  && r.imports[0].path[1] == L"strings",
              "import std.strings.*");
        check(!r.imports[1].wildcard && r.imports[1].alias == L"tun",
              "import ... as alias");
        check(r.imports[2].names.size() == 2 && r.imports[2].names[1].first == L"other"
                  && r.imports[2].names[1].second == L"alias",
              "selective import with rename");
        check(r.imports[3].isExport && r.imports[3].wildcard, "export re-export");
    }

    std::wstring t = L"foo.bar!(x) `dyn 42";
    int s = -1;
    check(identifierAt(t, 5, &s) == L"bar!" && s == 4, "identifierAt inside a word");
    check(identifierAt(t, 8, &s) == L"bar!" && s == 4, "identifierAt just after the !");
    check(identifierAt(t, 3, &s) == L"foo" && s == 0, "identifierAt at the end of a word");
    check(identifierAt(t, 13, &s) == L"`dyn", "identifierAt includes a backtick prefix");
    check(identifierAt(t, 18, &s).empty(), "identifierAt on a number is empty");
    check(identifierAt(t, 11, &s).empty(), "identifierAt on punctuation is empty");
}

// ---------------------------------------------------------------------------

static void write(fs::path const& p, std::string const& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}

static void test_finder() {
    std::print("Test: definition finder across modules\n");
    fs::path root = fs::temp_directory_path() / "tzpl_search_test";
    fs::remove_all(root);
    write(root / "proj/main.x",
          "import util.*;\n"
          "import geo as g;\n"
          "import bridge.{scale};\n"
          "import solo;\n"
          "fn ramp(n Int) Int { n }\n"
          "fn go() Void {\n"
          "    fn ramp(x Float) Float { x }\n"
          "    ramp(1);\n"
          "    g.area(2.0);\n"
          "}\n");
    write(root / "proj/util.x",
          "fn ramp(a Int, b Int) Int { a }\n"
          "private fn ramp(s String) String { s }\n"
          "fn _ramp() Void {}\n");
    write(root / "lib/geo.x",
          "fn area(r Float) Float { r }\n"
          "fn ramp() Void {}\n");
    write(root / "lib/bridge.x",
          "export deep.*;\n"
          "fn scale(x Int) Int { x }\n");
    write(root / "lib/deep.x",
          "fn scale(x Float) Float { x }\n"
          "fn ramp(q Int) Int { q }\n");
    write(root / "lib/solo.x", "fn ramp() Void {}\n");

    DefinitionQuery q;
    q.path = (root / "proj/main.x").string();
    readTextFileWide(q.path, q.text);
    q.searchDirs = { (root / "lib").string() };

    // Inside go(): the local overload, then the file's, then util's.
    q.caret = (int)q.text.find(L"ramp(1)") + 1;
    auto r = findDefinitions(q);
    check(r.name == L"ramp" && r.qualifier.empty(), "identifier at caret");
    check(r.hits.size() == 3, "three definitions in scope");
    if (r.hits.size() == 3) {
        check(r.hits[0].path.empty() && !r.hits[0].def.topLevel,
              "innermost: the local fn inside go()");
        check(r.hits[1].path.empty() && r.hits[1].def.topLevel,
              "then the file's top-level overload");
        check(r.hits[2].modulePath == L"util" && r.hits[2].def.line == 0,
              "then util's exported overload (private/_ ones skipped)");
    }
    check(r.unresolvedModules.empty(), "every import resolved");

    // At top level the local overload is out of scope.
    q.caret = (int)q.text.find(L"fn ramp(n") + 4;
    r = findDefinitions(q);
    check(r.hits.size() == 2, "top level: file + util only");

    // Qualified access through the alias.
    q.caret = (int)q.text.find(L"g.area") + 3;
    r = findDefinitions(q);
    check(r.qualifier == L"g" && r.hits.size() == 1
              && r.hits[0].modulePath == L"geo",
          "g.area resolves through the alias to geo.x only");

    // Selective import follows an export chain.
    q.caret = (int)q.text.find(L"{scale}") + 2;
    r = findDefinitions(q);
    check(r.hits.size() == 2, "scale: bridge's own plus the one it re-exports");

    // An in-memory override wins over the disk copy.
    q.readFile = [&](std::string const& p, std::wstring& out) {
        if (p.find("util.x") != std::string::npos) {
            out = L"fn ramp(z Int) Int { z }\nfn ramp(z Float) Float { z }\n";
            return true;
        }
        return readTextFileWide(p, out);
    };
    q.caret = (int)q.text.find(L"ramp(1)") + 1;
    r = findDefinitions(q);
    check(r.hits.size() == 4, "unsaved editor text is what gets searched");

    // Missing module reported, not fatal.
    q.readFile = nullptr;
    q.text = L"import nowhere.*;\nfn f() Void { ramp(); }\n";
    q.caret = (int)q.text.find(L"ramp()") + 1;
    r = findDefinitions(q);
    check(r.hits.empty() && r.unresolvedModules.size() == 1
              && r.unresolvedModules[0] == L"nowhere",
          "unresolvable import is reported");

    fs::remove_all(root);
}

int main() {
    std::print("=== Search tests ===\n\n");
    test_matcher();
    test_scanner();
    test_finder();
    std::print("\n=== {} passed, {} failed ===\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
