#include "logging.H"
#include "testassert.H"
#include "test2.H"

#include "testassert.tmpl"
#include "test2.tmpl"

static testmodule __testcomputeagent(
    "testassert",
    list<filename>::mk("testassert.H", "testassert.tmpl"),
    "values", [] {
        {   auto &t(T(5));
            assert(!strcmp(t.field().c_str(), "5"));
            assert(t.eval() == 5);
            assert(!strcmp(t.field().c_str(), "5"));
            delete &t; }
        {   auto &t(T('a'));
            assert(!strcmp(t.field().c_str(), "a"));
            assert(t.eval() == 'a');
            assert(!strcmp(t.field().c_str(), "a"));
            delete &t; }
        {   auto &t(T(7.25));
            assert(!strcmp(t.field().c_str(), "7.25"));
            assert(t.eval() == 7.25);
            assert(!strcmp(t.field().c_str(), "7.25"));
            delete &t; }
        {   int x;
            x = 92;
            auto &t(T(x));
            assert(!strcmp(t.field().c_str(), "x{...}"));
            x = 97;
            assert(!strcmp(t.field().c_str(), "x{...}"));
            assert(t.eval() == 97);
            assert(!strcmp(t.field().c_str(), "x{97}"));
            x = 93;
            assert(!strcmp(t.field().c_str(), "x{97}"));
            delete &t; } },
    "T2", [] {
        auto &t(T2(int,  ([] { return 3; })()));
        assert(t.eval() == 3);
        assert(!strcmp(t.field().c_str(), "([] { return 3; })(){3}"));
        delete &t; },
    "deref", [] {
        int x = 5;
        int &y(x);
        auto &t(T(y));
        assert(t.eval() == 5);
        logmsg(loglevel::info, t.field().c_str());
        delete &t; },
    "expressions", [] {
        {   int y = 5;
            auto &t(T(y) + T(7));
            assert(!strcmp(t.field().c_str(), "(y{...} + 7)"));
            assert(t.eval() == 12);
            assert(!strcmp(t.field().c_str(), "(y{5} + 7)"));
            delete &t; }
        {   int y = 5;
            int x = 7;
            auto &t(T(y) > T(x));
            assert(!strcmp(t.field().c_str(), "(y{...} > x{...})"));
            assert(t.eval() == false);
            assert(!strcmp(t.field().c_str(), "(y{5} > x{7})"));
            delete &t; }
        {   int y = 5;
            int x = 7;
            auto &t(T(x) > T(y));
            assert(!strcmp(t.field().c_str(), "(x{...} > y{...})"));
            assert(t.eval() == true);
            assert(!strcmp(t.field().c_str(), "(x{7} > y{5})"));
            delete &t; }
        {   auto &t(T(5) >= T(5));
            assert(t.eval() == true);
            assert(!strcmp(t.field().c_str(), "(5 >= 5)"));
            delete &t; }
        {   auto &t(T(5) <= T(5));
            assert(t.eval() == true);
            assert(!strcmp(t.field().c_str(), "(5 <= 5)"));
            delete &t; }
        {   int y = 5;
            int x = 7;
            auto &t(T(x) < T(y));
            assert(!strcmp(t.field().c_str(), "(x{...} < y{...})"));
            assert(t.eval() == false);
            assert(!strcmp(t.field().c_str(), "(x{7} < y{5})"));
            delete &t; }
        {   auto &t(T(5.25) - T(4.0));
            assert(t.eval() == 1.25);
            assert(!strcmp(t.field().c_str(), "(5.25 - 4)"));
            delete &t; }
        {   auto &t(T(string("hello")) == T(string("bar")));
            assert(t.eval() == false);
            assert(!strcmp(t.field().c_str(),
                           "(string(\"hello\"){hello} == "
                           "string(\"bar\"){bar})"));
            delete &t; }
        {   auto &t(T(string("foo")) == T(string("foo")));
            assert(t.eval() == true);
            assert(!strcmp(t.field().c_str(),
                           "(string(\"foo\"){foo} == "
                           "string(\"foo\"){foo})"));
            delete &t; }
        {   bool f = false;
            bool t = true;
            auto &tt(T(f) && T(t));
            assert(!strcmp(tt.field().c_str(), "(f{...} && t{...})"));
            assert(tt.eval() == false);
            assert(!strcmp(tt.field().c_str(), "(f{FALSE} && t{...})"));
            delete &tt; }
        {   bool f = false;
            bool t = true;
            auto &tt(T(t) && T(f));
            assert(!strcmp(tt.field().c_str(), "(t{...} && f{...})"));
            assert(tt.eval() == false);
            assert(!strcmp(tt.field().c_str(), "(t{TRUE} && f{FALSE})"));
            delete &tt; } } );
