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
            assert(l.empty()); });
    testcaseV("list", "contains", [] {
            assert(!list<int>::mk().contains(7));
            assert(!list<int>::mk().contains(0));
            assert(!list<int>::mk(1).contains(0));
            assert(!list<int>::mk(1,2,3).contains(0));
            assert(list<int>::mk(1,2,3).contains(1));
            assert(list<int>::mk(1,2,3).contains(2));
            assert(list<int>::mk(1,2,3).contains(3));
            auto l(list<int>::mk(1,2,3));
            l.pophead();
            assert(!l.contains(1)); });
    testcaseV("list", "mklist", [] {
            auto l(mklist(1,2,3));
            assert(l == list<int>::mk(1,2,3)); });
    testcaseV("list", "sort", [] {
            list<int> empty;
            sort(empty);
            assert(empty.empty());
            auto one(mklist(5));
            sort(one);
            assert(one == mklist(5));
            auto two1(mklist(5, 6));
            sort(two1);
            assert(two1 == mklist(6, 5));
            auto two2(mklist(6, 5));
            sort(two2);
            assert(two2 == mklist(6, 5));
            auto two3(mklist(5, 6));
            sort<int>(two3, [] (const int &a, const int &b) { return a > b; });
            assert(two3 == mklist(5, 6)); }); }
