#include "map.H"
#include "string.H"
#include "test.H"

#include "map.tmpl"
#include "serialise.tmpl"

void
tests::_map() {
    testcaseV("map", "basics", [] {
            map<int, string> m;
            assert(m.get(5) == Nothing);
            m.set(5, "hello");
            assert(m.get(5) == "hello");
            m.set(6, "goodbye");
            assert(m.get(5) == "hello");
            assert(m.get(6) == "goodbye");
            m.clear(6);
            assert(m.get(6) == Nothing);
            m.set(6, "dead");
            assert(m.get(6) == "dead");
            assert(m.get(5) == "hello");
            map<int, string> m2(m);
            assert(m.get(5) == "hello");
            assert(m.get(6) == "dead");
            assert(m2.get(5) == "hello");
            assert(m2.get(6) == "dead");
            m2.clear(5);
            assert(m.get(5) == "hello");
            assert(m.get(6) == "dead");
            assert(m2.get(5) == Nothing);
            assert(m2.get(6) == "dead");
            map<int, string> m3(Steal, m);
            assert(m.get(5) == Nothing);
            assert(m.get(6) == Nothing);
            assert(m3.get(5) == "hello");
            assert(m3.get(6) == "dead"); });
    testcaseV("map", "isprime", [] {
            typedef map<int, int> iT;
            /* Exhaustive for small numbers */
            assert(!iT::isprime(0));
            assert(!iT::isprime(1));
            assert(iT::isprime(2));
            assert(iT::isprime(3));
            assert(!iT::isprime(4));
            assert(iT::isprime(5));
            assert(!iT::isprime(6));
            assert(iT::isprime(7));
            assert(!iT::isprime(8));
            assert(!iT::isprime(9));
            assert(!iT::isprime(10));
            /* And pick a few arbitrary bigger ones */
            assert(iT::isprime(9623));
            assert(!iT::isprime(9624));
            assert(iT::isprime(99787));
            assert(!iT::isprime(99788));
            assert(!iT::isprime(999982));
            assert(iT::isprime(999983));
            assert(!iT::isprime(999984));
            assert(!iT::isprime(11715730));
            assert(iT::isprime(11715731));
            assert(!iT::isprime(11715732));
            assert(!iT::isprime(1003061836));
            assert(iT::isprime(1003061837));
            assert(!iT::isprime(1003061838)); });
    testcaseV("map", "nextsize", [] {
            typedef map<int, int> iT;
            unsigned c = 0;
            for (unsigned x = 0; x < 9; x++) {
                unsigned n = iT::nextsize(c);
                if (c != 0) assert(c == iT::prevsize(n));
                else assert(iT::prevsize(n) > 0);
                /* Aiming for a factor of eight each time. */
                assert(n > c * 7);
                if (c != 0) assert(n < c * 9);
                assert(iT::isprime(n));
                c = n; } });
    testcaseV("map", "collisions", [] {
            /* Engineer a lot of collisions in a small table (because
             * we know the hash function). */
            map<int, int> m;
            for (unsigned x = 1; x < 5; x++) {
                m.set(x * 7, x * 3);
                for (unsigned y = 1; y <= x; y++) {
                    assert(m.get(y * 7) == y * 3); } }
            unsigned nr = 0;
            for (auto it(m.start()); !it.finished(); it.next()) {
                assert(nr < 5);
                assert(m.get(it.key()) == it.value());
                nr++; } });
    testcaseV("map", "growshrink", [] {
            /* Insert a load of entries in the table, so that it
             * grows, and then take them back out again, so that it
             * shrinks. */
            map<int, long> m;
            for (unsigned x = 0; x < 1000000; x++) {
                assert(m.get(x) == Nothing);
                m.set(x, x+1);
                assert(m.get(x) == x+1);
                assert(m.nritems == x+1);
                assert(m.nrbuckets >= x / 10);
                assert(m.nrbuckets <= x * 10 + 10); }
            for (unsigned x = 0; x < 1000000; x++) {
                m.clear(x);
                assert(m.get(x) == Nothing);
                assert(m.nritems == 999999 - x);
                assert(m.nrbuckets >= m.nritems / 10);
                assert(m.nrbuckets <= m.nritems * 10 + 10); } });
    testcaseV("map", "immediate", [] {
            map<int, int> m(5,6, 10,11);
            assert(m.get(5) == 6);
            assert(m.get(10) == 11);
            assert(m.get(11) == Nothing); });
    testcaseV("map", "==", [] {
            typedef map<int, int> T;
            assert(T() == T());
            assert(T() != T(1,2));
            assert(T(1,2) == T(1,2));
            assert(T(1,2) != T(1,3));
            assert(T(1,2, 3,4) == T(1,2, 3,4));
            assert(T(1,2, 3,4) == T(3,4, 1,2));
            /* Hash collission */
            assert(T(1,2, 8,3) == T(1,2, 8,3));
            assert(T(1,2, 8,3) != T(1,2, 8,2));
            assert(T(1,2, 8,3) != T(1,2));
            /* Check that it works when they have different
             * nrbuckets */
            T m1;
            for (unsigned x = 0; x < 100; x++) {
                unsigned k;
                do { k = (int)random(); } while (m1.get(k) != Nothing);
                m1.set(k, (int)random()); }
            T m2(m1);
            assert(m2.nrbuckets != 23);
            m2.rehash(23);
            assert(m1 == m2);
            m2.set(53, m2.get(53).dflt(5)+3);
            assert(m1 != m2); });
    testcaseV("map", "strings", [] {
            assert(default_hashfn(string("hello")) !=
                   default_hashfn(string("goodbye")));
            assert(default_hashfn(string("hello")) ==
                   default_hashfn(string("hello")));
            map<string, string> m("hello", "bob",
                                  "goodbye", "bob",
                                  "greetings", "charlie");
            assert(m.get("hello") == "bob");
            assert(m.get("goodbye") == "bob");
            assert(m.get("greetings") == "charlie");
            m.clear("hello");
            assert(m == (map<string, string>("goodbye", "bob",
                                             "greetings", "charlie"))); });
    testcaseV("map", "iterator", [] {
            map<int, int> m(5, 7, 1, 2, 3, 4);
            bool a(false);
            bool b(false);
            bool c(false);
            for (auto it(m.start()); !it.finished(); it.next()) {
                if (it.key() == 5) {
                    assert(!a);
                    assert(it.value() == 7);
                    a = true; }
                else if (it.key() == 1) {
                    assert(!b);
                    assert(it.value() == 2);
                    b = true; }
                else if (it.key() == 3) {
                    assert(!c);
                    assert(it.value() == 4);
                    c = true; }
                else abort(); }
            assert(a);
            assert(b);
            assert(c); });
    testcaseV("map", "serialise", [] {
            quickcheck q;
            /* The random map generator is expensive enough that we
             * can't just accept the usual 1000 iterations (especially
             * when it's full of strings). */
            serialise<map<int, int> >(q, 100);
            serialise<map<string, int> >(q, 10); });
}
