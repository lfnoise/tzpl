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

#include "synthdef_pretty_print.hpp"
#include "synthdef_str_util.hpp"

namespace synthdef {

int len(PPDoc const& p)
{
	switch (p.index()) {
		case ppText: return std::get<PPText>(p).n;
		case ppList: return std::get<PPList>(p).n;
	}
	return 0;
}

int seplen(Separator sep)
{
	switch (sep) {
		case sepNone : return 0;
		case sepSpace : return 1;
		case sepComma : return 2;
		case sepColon : return 2;
	}
	return 0;
}

int calcLen(vector<PPDoc> const& fv)
{
	int n = 0;
	for (PPDoc const& p : fv) {
		n += len(p);
	}
	return n;
}

string separatorString(Separator separator)
{
	switch (separator) {
		case sepNone : return "";
		default:
		case sepSpace : return " ";
		case sepComma : return ", ";
		case sepColon : return ": ";
	}
}

string flat(PPDoc const& d);
string flat(PPList const& list);
string _layout(PPDoc const& d, int maxWidth, int ribbonWidth, int indent, int& column);

string flat(PPDoc const& f)
{
	switch (f.index()) {
		case ppText: return std::get<PPText>(f).s;
		case ppList: return flat(std::get<PPList>(f));
	}
	return "";
}

u32 closingBracket(u32 c) {
    switch(c) {
        case 0 : return 0;
        case '(' : return ')';
        case '[' : return ']';
        case '{' : return '}';
        case '<' : return '>';
        case '"' : return '"';
        case '`' : return '`';
        case '\'' : return '\'';
        case 0xAB : return 0xBB; // french double
        case 0x2039 : return 0x203A; // french single
        default: return 0;
    }
}

string flat(PPList const& list)
{
	string out = encodeRune(list.brace);
	string sep = separatorString(list.separator);
	bool once = true;
	for (PPDoc const& d : list.v) {
		if (once) once = false;
		else out += sep;
		out += flat(d);
	}
	out += encodeRune(closingBracket(list.brace));
	return out;
}


string nested(PPList const& list, int maxWidth, int ribbonWidth, int indent, int& column)
{
	string sep = separatorString(list.separator);
	string out = list.brace ? encodeRune(list.brace) : "";
	int bracelen = list.brace ? 1 : 0;
	column += bracelen;
	if (list.noLineBreak) {
		assert(list.v.size() == 2);
				int tab = std::max(0, indent - column);
				spaces(out, tab);
				column = indent;
		out += _layout(list.v[0], maxWidth, ribbonWidth, indent, column);
		out += sep;
		out += _layout(list.v[1], maxWidth, ribbonWidth, indent, column);
	} else {
		//printf("col %d indent %d %d\n", column, indent, indent+4);
		indent += 4;
		string spacing;
		//dotindent(spacing, indent);
		spaces(spacing, indent);
		int i = 0;
		for (PPDoc const& d : list.v) {
			if (i > 0) out += sep;
			if (column < indent) {
				//once = false;
//				if (i > 0) {
//					int tab = std::max(0, indent - column -1);
//					spaces(out, tab);
//					column += tab;
//				}
				out += _layout(d, maxWidth, ribbonWidth, indent, column);
			} else {
				//out += "B";
				out += "\n" + spacing;
				column = indent;
				out += _layout(d, maxWidth, ribbonWidth, indent, column);
			}
			++i;
		}
	}
	out += list.brace ? encodeRune(closingBracket(list.brace)) : "";
    column += 1;
	return out;
}

string _layout(PPDoc const& f, int maxWidth, int ribbonWidth, int indent, int& column)
{
	switch (f.index()) {
		case ppText : {
			PPText const& text = std::get<PPText>(f);
            column += text.n;
			return text.s;
		}
		case ppList : {
			PPList const& list = std::get<PPList>(f);
			if (list.n <= ribbonWidth && indent + list.n <= maxWidth) {
                column += list.n;
				return flat(list);
			} else {
				auto out = nested(list, maxWidth, ribbonWidth, indent, column);
				return out;
			}
		}
	}
	return "";
}

string layout(PPDoc const& f, int maxWidth, int ribbonWidth, int indent)
{
	string spacing;
	//dotindent(spacing, indent);
	spaces(spacing, indent);
    int column = 0;
    return spacing + _layout(f, maxWidth, ribbonWidth, indent, column);
}

// create a ppdoc from a string
PPDoc ppdoc(string s) {
	return PPDoc(PPText{s, int(s.length())});
}

// create a ppdoc from a vector of ppdocs
PPDoc ppdoc(vector<PPDoc> const& ds, u32 brace, Separator sep, bool noLineBreak)
{
	const int parens = 2 ;
	const int separators = (int(ds.size()) - 1) * 	seplen(sep);
	int n = parens + separators + calcLen(ds);
	return PPDoc(PPList{std::move(ds), n, brace, sep, noLineBreak});
}

bool isOneLine(string s)
{
	int newlines = 0;
	for (auto c : s) {
		if (c == '\n') ++newlines;
	}
	return !(newlines > 1 || (newlines == 1 && s[s.length()-1] == '\n'));
}

string blockFmt(string s)
{
	return isOneLine(s) ? s : string("\n") + s;
}


}
