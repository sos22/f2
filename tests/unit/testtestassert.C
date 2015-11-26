#include "logging.H"
#include "testassert.H"
#include "test2.H"

#include "testassert.tmpl"
#include "test2.tmpl"

#define assertstr(x, y)                                                 \
    do {                                                                \
        auto __x((x).field().c_str());                                  \
        auto __y(y);                                                    \
        if (strcmp(__x, __y)) {                                         \
            logmsg(loglevel::emergency,                                 \
                   fields::mk(__x) + " != " + fields::mk(__y));         \
            abort(); }                                                  \
    } while (0)

static testmodule __testcomputeagent(
    "testassert",
    list<filename>::mk("testassert.H", "testassert.tmpl"),
    "values", [] {
        {   auto &t(T(5));
            assertstr(t, "5");
            assert(t.eval() == 5);
            assertstr(t, "5");
            delete &t; }
        {   auto &t(T('a'));
            assertstr(t, "a");
            assert(t.eval() == 'a');
            assertstr(t, "a");
            delete &t; }
        {   auto &t(T(7.25));
            assertstr(t, "7.25");
            assert(t.eval() == 7.25);
            assertstr(t, "7.25");
            delete &t; }
        {   int x;
            x = 92;
            auto &t(T(x));
            assertstr(t, "x{...}");
            x = 97;
            assertstr(t, "x{...}");
            assert(t.eval() == 97);
            assertstr(t, "x{97}");
            x = 93;
            assertstr(t, "x{97}");
            delete &t; } },
    "T2", [] {
        auto &t(T2(int,  ([] { return 3; })()));
        assert(t.eval() == 3);
        assertstr(t, "([] { return 3; })(){3}");
        delete &t; },
    "deref", [] {
        int x = 5;
        int &y(x);
        auto &t(T(y));
        assert(t.eval() == 5);
        assertstr(t, "y{5}");
        delete &t; },
    "precedence", [] {
        {   auto &t(T(true) && T(true) && T(true));
            assertstr(t, "TRUE && TRUE && TRUE");
            delete &t; }
        {   auto &t(T(5) + T(7) + T(3));
            assertstr(t, "5 + 7 + 3");
            delete &t; }
        {   auto &t(T(5) * T(7) + T(3));
            assertstr(t, "5 * 7 + 3");
            delete &t; }
        {   auto &t(T(5) * (T(7) + T(3)));
            assertstr(t, "5 * (7 + 3)");
            delete &t; } },
    "expressions", [] {
        {   int y = 5;
            auto &t(T(y) + T(7));
            assertstr(t, "y{...} + 7");
            assert(t.eval() == 12);
            assertstr(t, "y{5} + 7");
            delete &t; }
        {   int y = 5;
            int x = 7;
            auto &t(T(y) > T(x));
            assertstr(t, "y{...} > x{...}");
            assert(t.eval() == false);
            assertstr(t, "y{5} > x{7}");
            delete &t; }
        {   int y = 5;
            int x = 7;
            auto &t(T(x) > T(y));
            assertstr(t, "x{...} > y{...}");
            assert(t.eval() == true);
            assertstr(t, "x{7} > y{5}");
            delete &t; }
        {   int y = 5;
            auto &t(T(y) * T(7) == T(35));
            assertstr(t, "y{...} * 7 == 35");
            assert(t.eval() == true);
            assertstr(t, "y{5} * 7 == 35");
            delete &t; }
        {   auto &t(T(5) >= T(5));
            assert(t.eval() == true);
            assertstr(t, "5 >= 5");
            delete &t; }
        {   auto &t(T(5) <= T(5));
            assert(t.eval() == true);
            assertstr(t, "5 <= 5");
            delete &t; }
        {   int y = 5;
            int x = 7;
            auto &t(T(x) < T(y));
            assertstr(t, "x{...} < y{...}");
            assert(t.eval() == false);
            assertstr(t, "x{7} < y{5}");
            delete &t; }
        {   auto &t(T(5.25) - T(4.0));
            assert(t.eval() == 1.25);
            assertstr(t, "5.25 - 4");
            delete &t; }
        {   auto &t(T(string("hello")) == T(string("bar")));
            assert(t.eval() == false);
            assertstr(t,
                      "string(\"hello\"){hello} == "
                      "string(\"bar\"){bar}");
            delete &t; }
        {   auto &t(T(string("foo")) == T(string("foo")));
            assert(t.eval() == true);
            assertstr(t,
                      "string(\"foo\"){foo} == "
                      "string(\"foo\"){foo}");
            delete &t; }
        {   bool f = false;
            bool t = true;
            auto &tt(T(f) && T(t));
            assertstr(tt, "f{...} && t{...}");
            assert(tt.eval() == false);
            assertstr(tt, "f{FALSE} && t{...}");
            delete &tt; }
        {   bool f = false;
            bool t = true;
            auto &tt(T(t) && T(f));
            assertstr(tt, "t{...} && f{...}");
            assert(tt.eval() == false);
            assertstr(tt, "t{TRUE} && f{FALSE}");
            delete &tt; }
        {   bool f = false;
            bool t = true;
            auto &tt(T(f) || T(t));
            assertstr(tt, "f{...} || t{...}");
            assert(tt.eval() == true);
            assertstr(tt, "f{FALSE} || t{TRUE}");
            delete &tt; }
        {   bool f = false;
            bool t = true;
            auto &tt(T(t) || T(f));
            assertstr(tt, "t{...} || f{...}");
            assert(tt.eval() == true);
            assertstr(tt, "t{TRUE} || f{...}");
            delete &tt; }
        {   auto &tt((T(true) || T(false)) && T(false));
            assertstr(tt, "(TRUE || FALSE) && FALSE");
            assert(tt.eval() == false);
            delete &tt; }
        {   auto &tt(T(true) || T(false) || T(false));
            assertstr(tt, "TRUE || FALSE || FALSE");
            assert(tt.eval() == true);
            delete &tt; }
        {   auto &tt(T(true) || (T(false) && T(false)));
            assertstr(tt, "TRUE || (FALSE && FALSE)");
            assert(tt.eval() == true);
            delete &tt; } } );
