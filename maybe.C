#include "maybe.H"

#include "fields.H"
#include "quickcheck.H"
#include "string.H"
#include "test.H"

#include "maybe.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"

#include "fieldfinal.H"

maybe<void>
maybe<void>::just;

void
tests::_maybe()
{
    class nodestruct {
    public: ~nodestruct() { abort(); }
    };
    class countcopies {
    public: int counter;
    public: explicit countcopies(int base) : counter(base) {}
    public: countcopies(const countcopies &&c) : counter(c.counter) {}
    public: countcopies(const countcopies &c) : counter(c.counter+1) {} };
    class notedestruct {
    public: bool &d;
    public: notedestruct(bool &_d) : d(_d) { assert(d == false); }
    public: ~notedestruct() { d = true; } };
    class cons1 {
    public: int val1;
    public: const char *val2;
    public: cons1(int _val1, const char *_val2)
        : val1(_val1),
          val2(_val2) {} };
    testcaseV("maybe", "nothing", [] {
            ::maybe<nodestruct> x(Nothing); });
    testcaseV("maybe", "copy", [] {
            ::maybe<countcopies> x(countcopies(0));
            ::maybe<countcopies> y(x);
            assert(x.just().counter == 1);
            assert(y.just().counter == 2); });
    testcaseV("maybe", "move", [] {
            ::maybe<countcopies> x(
                std::move(::maybe<countcopies>(std::move(countcopies(0)))));
            assert(x.just().counter == 1); });
    testcaseV("maybe", "destruct", [] {
            bool dead = false;
            {   ::maybe<notedestruct> aaa = notedestruct(dead);
                assert(&aaa.just().d == &dead);
                dead = false; }
            assert(dead); });
    testcaseV("maybe", "assign", [] {
            bool dead = false;
            ::maybe<notedestruct> a = notedestruct(dead);
            dead = false;
            a = Nothing;
            assert(dead == true); });
    testcaseV("maybe", "assign2", [] {
            ::maybe<int> a(7);
            a = 8;
            assert(a.just() == 8); });
    testcaseV("maybe", "consconst", [] {
            int x = 7;
            const int &y(x);
            ::maybe<int> z(y);
            assert(z.just() == 7); });
    testcaseV("maybe", "isnothing", [] {
            assert(!::maybe<int>(Nothing));
            assert(!!::maybe<int>(7)); });
    testcaseV("maybe", "dflt", [] {
            assert(::maybe<int>(Nothing).dflt(8) == 8);
            assert(::maybe<int>(9).dflt(8) == 9); });
    testcaseV("maybe", "!=", [] {
            assert(::maybe<int>(7) != Nothing);
            assert(Nothing != ::maybe<int>(7));
            assert(::maybe<int>(7) != ::maybe<int>(8));
            assert(::maybe<int>(Nothing) == Nothing);
            assert(Nothing == ::maybe<int>(Nothing));
            assert(::maybe<int>(7) == ::maybe<int>(7));
            assert(::maybe<int>(7) != 8);
            assert(!(::maybe<int>(7) != 7));
            assert(7 == maybe<int>(7));
            assert(!(7 == maybe<int>(Nothing)));
            assert(!(8 == maybe<int>(7))); });
    testcaseV("maybe", "map", [] {
            assert(::maybe<int>(7)
                   .map<char>([] (const int &x) { return x == 7 ? 'Y' : 'N'; })
                   == 'Y');
            assert(::maybe<int>(8)
                   .map<char>([] (const int &x) { return x == 7 ? 'Y' : 'N'; })
                   == 'N');
            assert(::maybe<int>(Nothing)
                   .map<char>([] (const int &x) { return x == 7 ? 'Y' : 'N'; })
                   == Nothing); });
    testcaseV("maybe", "void", [] {
            assert(::maybe<void>(Nothing) == Nothing);
            assert(Nothing == ::maybe<void>(Nothing));
            assert(::maybe<void>::just != Nothing);
            assert(::maybe<void>::just == ::maybe<void>(::maybe<void>::just));
            ::maybe<void> x(Nothing);
            assert(x == Nothing);
            x = ::maybe<void>::just;
            assert(x != Nothing); });
    testcaseV("maybe", "qc", [] {
            bool havenothing = false;
            bool haveanyjust = false;
            bool havediffjust = false;
            unsigned firstjust;
            quickcheck q;
            for (unsigned x = 0; x < 10; x++) {
                ::maybe<unsigned> y(q);
                if (y == Nothing) {
                    havenothing = true; }
                else if (!haveanyjust) {
                    haveanyjust = true;
                    firstjust = y.just(); }
                else if (firstjust != y.just()) {
                    havediffjust = true; } }
            assert(havenothing);
            assert(haveanyjust);
            assert(havediffjust); });
    testcaseV("maybe", "=", [] {
            maybe<int> x(7);
            maybe<int> y(Nothing);
            y = x;
            assert(y == 7); });
    testcaseV("maybe", "mkjust", [] {
            maybe<cons1> c(Nothing);
            const char *s = "Hello";
            c.mkjust(71, s);
            assert(c.isjust());
            assert(c.just().val1 == 71);
            assert(c.just().val2 == s); });
    testcaseV("maybe", "field", [] {
            assert(!strcmp(maybe<string>(Nothing).field().c_str(),
                           "Nothing"));
            assert(!strcmp(maybe<string>("foo").field().c_str(),
                           "<foo>")); });
    testcaseV("maybe", "serialise", [] {
            quickcheck q;
            serialise<maybe<unsigned> >(q);
            serialise<maybe<string> >(q); });
    testcaseV("maybe", "parser", [] {
            parsers::roundtrip(
                parsers::_maybe(parsers::intparser<unsigned>())); });
}
