#include "list.H"
#include "test.H"

#include "list.tmpl"

void
tests::_list() {
    testcaseV("list", "pushtail", [] {
            list<int> l;
            l.pushtail(5,6,7,8);
            assert(l.pophead() == 5);
            assert(l.poptail() == 8);
            l.pushtail(9);
            assert(l.pophead() == 6);
            assert(l.pophead() == 7);
            assert(l.pophead() == 9);
            assert(l.empty()); });
    testcaseV("list", "mk", [] {
            auto l(list<int>::mk(5,6,7,8));
            assert(l.pophead() == 5);
            assert(l.pophead() == 6);
            assert(l.pophead() == 7);
            assert(l.pophead() == 8);
            assert(l.empty()); }); }
