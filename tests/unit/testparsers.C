#include "parsers.H"
#include "test2.H"

#include "parsers.tmpl"
#include "test2.tmpl"

using namespace parsers;

static testmodule __testparsers(
    "parsers",
    list<filename>::mk("parsers.C", "parsers.H", "parsers.tmpl"),
    testmodule::LineCoverage(99_pc),
    testmodule::BranchCoverage(35_pc),
    "strmatcher", [] {
        const char *e = "";
        const char *A = "A";
        const char *AB = "AB";
        assert(strmatcher("").parse(e).success() == e);
        assert(strmatcher("A").parse(e).failure() == error::noparse);
        assert(strmatcher("").parse(A).success() == A);
        assert(strmatcher("A").parse(A).success() == A + 1);
        assert(strmatcher("A").parse(AB).success() == AB+1);
        assert(strmatcher("A",7).match("A").success() == 7); },
    "voidmatch", [] {
        assert(strmatcher("ABC").match("ABC") == Success);
        assert(strmatcher("ABC").match("A") == error::noparse);
        assert(strmatcher("ABC").match("ABCD") == error::noparse);
        assert(strmatcher("").match("A") == error::noparse);
        assert(strmatcher("").match("") == Success); },
    "strparser", [] {
        const char *XXX = "XXX";
        const char *XXXYYY = "XXX YYY";
        const char *_123YYY = "123+YYY";
        assert(strparser.match("").failure() == error::noparse);
        {   auto r(strparser.parse(XXX));
            assert(!strcmp(r.success().res, "XXX"));
            assert(r.success().left == XXX+3); }
        {   auto r(strparser.parse(XXXYYY));
            assert(!strcmp(r.success().res, "XXX"));
            assert(r.success().left == XXXYYY+3); }
        {   auto r(strparser.parse(_123YYY));
            assert(!strcmp(r.success().res, "123"));
            assert(r.success().left == _123YYY+3); }
        assert(strparser.match("\"").failure() == error::noparse);
        {   const char *ee = "\"\"";
            auto r(strparser.parse(ee));
            assert(!strcmp(r.success().res, ""));
            assert(r.success().left == ee + 2); }
        {   const char *ee = "\"X\"";
            auto r(strparser.parse(ee));
            assert(!strcmp(r.success().res, "X"));
            assert(r.success().left == ee + 3); }
        {   const char *ee = "\"\\\\\"";
            auto r(strparser.parse(ee));
            assert(!strcmp(r.success().res, "\\"));
            assert(r.success().left == ee + 4); }
        assert(strparser.match("\"\\\"").failure() == error::noparse);
        {   const char *ee = "\"\\\\\"";
            auto r(strparser.parse(ee));
            assert(!strcmp(r.success().res, "\\"));
            assert(r.success().left == ee + 4); }
        {   const char *ee = "\"\\\"\"";
            auto r(strparser.parse(ee));
            assert(!strcmp(r.success().res, "\""));
            assert(r.success().left == ee + 4); }
        {   const char *ee = "\"\\x07\"";
            auto r(strparser.parse(ee));
            assert(!strcmp(r.success().res, "\x07"));
            assert(r.success().left == ee + 6); }
        {   const char *ee = "\"\\xaa\"";
            auto r(strparser.parse(ee));
            assert(!strcmp(r.success().res, "\xaa"));
            assert(r.success().left == ee + 6); }
        {   const char *ee = "\"\\xAA\"";
            auto r(strparser.parse(ee));
            assert(!strcmp(r.success().res, "\xAA"));
            assert(r.success().left == ee + 6); }
        assert(strparser.match("\"\\xZ\"") == error::noparse);
        assert(strparser.match("\"\\F\"") == error::noparse);
        assert(strparser.match("\"\x01\"") == error::noparse); },
    "fuzzstrparser", [] {
        for (unsigned x = 0; x < 1000; x++) {
            const char *s = quickcheck();
            ::fields::fieldbuf b;
            ::fields::mk(s).escape().fmt(b);
            auto res(strparser.match(b.c_str()));
            assert(res.issuccess());
            assert(!strcmp(res.success(), s)); } },
    "errorparser", [] {
        auto r(errparser<int>(error::underflowed).parse((const char *)27));
        assert(r.failure() == error::underflowed);},
    "intparser", [] {
        auto ip(intparser<long>);
        assert(ip().match("").failure() == error::noparse);
        assert(ip().parse("ba").failure() == error::noparse);
        assert(ip().match("7").success() == 7);
        assert(ip().match("72").success() == 72);
        assert(ip().match("-72").success() == -72);
        assert(ip().match("123,456").success() == 123456);
        assert(ip().match("ab").failure() == error::noparse);
        assert(ip().match("{16}ab").success() == 0xab);
        assert(ip().match("{16}AB").success() == 0xab);
        assert(ip().match("{16}Ab").success() == 0xab);
        assert(ip().match("+7") == 7);
        assert(ip().match("000000") == 0);
        assert(ip().match("{5}0") == 0);
        assert(ip().match("{52}0") == error::noparse);
        assert(ip().match("{Z}0") == error::noparse);
        assert(ip().match("{93}1") == error::noparse);
        assert(ip().match("{1") == error::noparse); },
    "algebra+", [] {
        auto ip(intparser<long>);
        assert( ("foo"+ip()).match("foo7").success() == 7);
        assert( (ip() + "bar").match("7bar").success() == 7);
        assert( !strcmp(
                    (strmatcher("foo")+strmatcher("bar"))
                    .parse("foobar")
                    .success(),
                    ""));
        auto r( (ip() + " " + ip()).match("83 -1"));
        assert(r.success().first() == 83);
        assert(r.success().second() == -1); },
    "algebra|", [] {
        auto ip(intparser<long>);
        assert( (strmatcher("foo")|ip())
                .match("foo")
                .success()
                .isnothing());
        assert( (strmatcher("foo")|ip())
                .match("83")
                .success() == 83);
        assert( (strmatcher("foo")|ip())
                .match("foo83")
                .failure()
                == error::noparse);
        assert( (strmatcher("foo")|ip())
                .match("ZZZ")
                .failure()
                == error::noparse);
        assert( (ip()|ip())
                .match("ZZZ")
                .failure()
                == error::noparse);
        assert( (ip()|strmatcher("bar"))
                .match("7")
                .success() == 7);
        assert( (ip()|strmatcher("bar"))
                .match("bar")
                .success()
                .isnothing());
        assert( (ip()|strmatcher("bar")).match("91bar")
                == error::noparse);
        assert( (ip()|strmatcher("bar"))
                .match("dgha")
                == error::noparse);
        assert((strmatcher("foo")|strmatcher("bar"))
               .parse("foo")
               .issuccess());
        assert((strmatcher("foo")|strmatcher("bar"))
               .parse("bar")
               .issuccess());
        assert((strmatcher("foo")|strmatcher("bar"))
               .parse("bazz")
               .failure() == error::noparse);
        assert((strmatcher("foo")|strmatcher("bar")|strmatcher("bazz"))
               .parse("foo")
               .issuccess());
        assert((strmatcher("foo")|strmatcher("bar")|strmatcher("bazz"))
               .parse("bar")
               .issuccess());
        assert((strmatcher("foo")|strmatcher("bar")|strmatcher("bazz"))
               .parse("bazz")
               .issuccess());
        assert((strmatcher("foo")|strmatcher("bar")|strmatcher("bazz"))
               .parse("boom")
               .failure() == error::noparse);
        assert((strmatcher("foo", 83)|strmatcher("bar",
                                                 (const char *)"hello"))
               .match("foo")
               .success()
               .left() == 83);
        assert(!strcmp(
                   (strmatcher("foo", 83)|strmatcher("bar",
                                                     (const char *)"hello"))
                   .match("bar")
                   .success()
                   .right(),
                   "hello")); },
    "algebra||", [] {
        auto &p(strmatcher("X").val(6) ||
                strmatcher("Y").val(7));
        assert(p.match("X") == 6);
        assert(p.match("Y") == 7);
        assert(p.match("Z") == error::noparse); },
    "algebra~", [] {
        auto &p(~strmatcher("X", 5) +
                intparser<unsigned>() +
                ~strmatcher("Y"));
        {   auto r(p.match("73"));
            assert(r.success().first().first() == Nothing);
            assert(r.success().first().second() == 73);
            assert(r.success().second() == Nothing); }
        {   auto r(p.match("73Y"));
            assert(r.success().first().first() == Nothing);
            assert(r.success().first().second() == 73);
            assert(r.success().second() != Nothing); }
        {   auto r(p.match("X73"));
            assert(r.success().first().first().just() == 5);
            assert(r.success().first().second() == 73);
            assert(r.success().second() == Nothing); }
        {   auto r(p.match("X73Y"));
            assert(r.success().first().first().just() == 5);
            assert(r.success().first().second() == 73);
            assert(r.success().second() != Nothing); }
        assert(p.match("X") == error::noparse);
        assert(p.match("Y") == error::noparse); },
    "intedges", [] {
        /* Surprise! Clang does the wrong thing with integer constants
           which are the most negative value for their type. */
        assert(intparser<long>()
               .match("{16}-8000,0000,0000,0000")
               == -0x7fffffffffffffffl - 1);
        assert(intparser<long>()
               .match("{16}-8000,0000,0000,0001")
               == error::overflowed);
        assert(intparser<long>()
               .match("{16}8000,0000,0000,0000")
               == error::overflowed);
        assert(intparser<long>()
                   .match("{16}7fff,ffff,ffff,ffff")
               == 0x7fffffffffffffffl);
        assert(intparser<int>()
               .match("{16}-8000,0000")
               == -0x7fffffff - 1);
        assert(intparser<int>()
               .match("{16}-8000,0001")
               == error::overflowed);
        assert(intparser<int>()
               .match("{16}8000,0000")
               == error::overflowed);
        assert(intparser<int>()
               .match("{16}7fff,ffff")
               == 0x7fffffff);
        assert(intparser<short>().match("{16}-8000") == -0x8000);
        assert(intparser<short>().match("{16}-8001") == error::overflowed);
        assert(intparser<short>().match("{16}8000") == error::overflowed);
        assert(intparser<short>().match("{16}7fff") == 0x7fffl);
        assert(intparser<char>().match("{16}-80") == -0x80);
        assert(intparser<char>().match("{16}-81") == error::overflowed);
        assert(intparser<char>().match("{16}80") == error::overflowed);
        assert(intparser<char>().match("{16}7f") == 0x7fl);
        assert(intparser<long>().match("0") == 0);
        assert(intparser<long>().match("000000") == 0);
        assert(intparser<unsigned long>().match("0") == 0);
        assert(intparser<unsigned long>().match("00") == 0);
        assert(intparser<unsigned long>().match("001") == 1);
        assert((intparser<long>() + "ns").match("0ns") == 0);
        assert(intparser<unsigned long>().match("{16}ffff,ffff,ffff,ffff")
               == 0xfffffffffffffffful);
        assert(intparser<unsigned long>().match("{16}1,0000,0000,0000,0000")
               == error::overflowed);
        assert(intparser<unsigned>().match("{16}ffff,ffff")
               == 0xfffffffful);
        assert(intparser<unsigned>().match("{16}1,0000,0000")
               == error::overflowed);
        assert(intparser<unsigned short>().match("{16}ffff")
               == 0xfffful);
        assert(intparser<unsigned short>().match("{16}1,0000")
               == error::overflowed);
        /* Base 36 is acceptable, base 37 isn't. */
        assert(intparser<unsigned>().match("{36}Za")
               == 1270);
        assert(intparser<unsigned>().match("{37}Za")
               == error::noparse); },
    "double", [] {
        assert(longdoubleparser.match("7.25") == 7.25);
        assert(longdoubleparser.match("-1") == -1);
        assert(longdoubleparser.match("Z") == error::noparse); },
    "nul", [] {
        assert(nulparser(73).match("") == 73);
        assert(nulparser(73).match("x") == error::noparse); },
    "map", [] {
        auto ip(intparser<long>);
        assert( ip()
                .map<double>([] (int x) { return x * 5 + .3; })
                .match("7")
                .success() == 35.3);
        assert( errparser<int>(error::ratelimit)
                .map<const char *>([] (int) { return "Huh?"; })
                .match("7")
                .failure() == error::ratelimit); },
    "maperr", [] {
        assert(intparser<unsigned>()
               ._maperr<double>(
                   [] (const orerror<unsigned> &x) {
                       return x.failure(); })
               .match("Z") == error::noparse);
        assert(intparser<unsigned>()
               ._maperr<double>(
                   [] (const orerror<unsigned> &x) {
                       assert(x.success() == 73);
                       return error::noparse; })
               .match("73") == error::noparse);
        assert(intparser<unsigned>()
               ._maperr<double>(
                   [] (const orerror<unsigned> &x) {
                       assert(x.success() == 73);
                       return 92.5; })
               .match("73") == 92.5);
        assert(intparser<unsigned>()
               ._maperr<double>(
                   [] (const orerror<unsigned> &x) {
                       assert(x == error::noparse);
                       return 92.25; })
               .match("") == 92.25);
        assert(intparser<unsigned>()
               .maperr<double>(
                   [] (const unsigned &x) {
                       return x + 5; })
               .match("12") == 17);
        assert(intparser<unsigned>()
               .maperr<double>(
                   [] (const unsigned &) {
                       return error::overflowed; })
               .match("12") == error::overflowed); },
    "mapvoid", [] {
        int cntr;
        cntr = 0;
        assert(strmatcher("Foo")
               .map<int>([&cntr] { return cntr++; })
               .match("Foo") == 0);
        assert(strmatcher("Foo")
               .map<int>([&cntr] { return cntr++; })
               .match("Foo") == 1);
        assert(strmatcher("Foo")
               .map<int>([&cntr] { return cntr++; })
               .match("Foo") == 2);
        assert(strmatcher("Foo")
               .map<int>([&cntr] { return cntr++; })
               .match("Bar") == error::noparse); },
    "roundtrip", [] { parsers::roundtrip(intparser<unsigned>()); },
    "sepby", [] {
        auto &p(parsers::sepby(parsers::intparser<int>(),
                               strmatcher(" ")));
        assert(p.match("") == list<int>::mk());
        assert(p.match("1") == list<int>::mk(1));
        assert(p.match("1 2 3") == list<int>::mk(1, 2, 3));
        assert(p.match("1 2 3 ") == error::noparse);
        assert(p.match(" 1 2 3") == error::noparse);
        assert((strmatcher("YYY") + p + strmatcher("XXX"))
               .match("YYY1 2 3XXX") ==
               list<int>::mk(1, 2, 3)); },
    "callcons", [] {
        class cons {
        private: cons() = delete;
        private: void operator=(const cons &) = delete;
        public:  cons(int a1, const char *a2) {
            assert(a1 == 5);
            assert(!strcmp(a2, "foo")); } };
        class p : public parser<cons> {
        private: orerror<result> parse(const char *m) const {
            if (strcmp(m, "X")) return error::noparse;
            else return orerror<result>(Success, m+1, 5, "foo"); } };
        auto &pp(*new p());
        assert(pp.match("X").issuccess());
        assert(pp.match("").isfailure());
        assert(pp.match("XX").isfailure()); } );
