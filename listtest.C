#include "list.H"
#include "string.H"
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
            assert(two1 == mklist(5, 6));
            auto two2(mklist(6, 5));
            sort(two2);
            assert(two2 == mklist(5, 6));
            auto two3(mklist(5, 6));
            sort<int>(two3, [] (const int &a, const int &b) { return a < b; });
            assert(two3 == mklist(6, 5)); });
    testcaseV("list", "=", [] {
            auto l1(mklist(5,6,7,8));
            l1 = list<int>::mk();
            assert(l1.empty());
            l1 = mklist(1,2,3);
            assert(l1 == mklist(1,2,3));
            l1 = mklist(4,5);
            assert(l1 == mklist(4,5)); });
    testcaseV("list", "partial", [] {
            list<string> l;
            auto p(l.mkpartial("foo"));
            p->val().truncate(1);
            auto p2(l.mkpartial("bar"));
            l.pushtail(p);
            auto p3(l.mkpartial());
            p3->val() = string("wibble");
            l.pushtail(p3);
            l.pushtail(p2);
            assert(l.pophead() == "f");
            auto p4(l.mkpartial());
            l.pushtail(p4) = string("moo");
            assert(l.pophead() == "wibble");
            assert(l.pophead() == "bar");
            assert(l.pophead() == "moo"); });
}
