#include "synthdef_types2.hpp"

namespace synthdef {

    enum Verbosity {
        terse, brief, normal, verbose
    };

    enum Separator {
        sepNone, sepSpace, sepComma, sepColon
    };

    struct PPText;
    struct PPList;

    enum { ppText, ppList };

    using PPDoc = variant<PPText, PPList>;

    struct PPText {
        string s;
        int n;
    };

    struct PPList {
        vector<PPDoc> v;
        int n;
        u32 brace;
        Separator separator;
        bool noLineBreak = false;
    };

    // create a ppdoc from a string
    PPDoc pp_text(string s);

    // create a ppdoc from a vector of ppdocs
    PPDoc pp_list(vector<PPDoc> const& ds, u32 brace, Separator sep, bool noLineBreak = false);

    //PPDoc ppdoc(Str head, PPDoc tail);

    const int kDefaultMaxWidth = 120;
    const int kDefaultRibbonWidth = 40;

    string layout(
        PPDoc const& d,
        int maxWidth = kDefaultMaxWidth,
        int ribbonWidth = kDefaultRibbonWidth,
        int indent = 0);

    int len(PPDoc const& p);

    string blockFmt(string s);

}
