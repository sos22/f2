#include "fields.H"
#include "spark.H"
#include "test2.H"

#include "spark.tmpl"
#include "test2.tmpl"

#include "fieldfinal.H"

using namespace fields;

#define simpletest(field, expected)                                     \
    do {                                                                \
        const char *__v = (field).c_str();                              \
        const char *__expected = (expected);                            \
        if (strcmp(__v, __expected)) {                                  \
            fprintf(stderr,                                             \
                    "%s: %d: %s: expected %s, got %s\n",                \
                    __FILE__,                                           \
                    __LINE__,                                           \
                    #field ,                                            \
                    __expected,                                         \
                    __v);                                               \
            abort(); } }                                                \
    while (0)

static testmodule __testfields(
    "fields",
    /* Ideally, tmpheap would have its own unit test, but it's covered
     * pretty thoroughly by this one, so this'll do for now. */
    list<filename>::mk("fields.C", "fields.H", "fieldfinal.H", "tmpheap.C"),
    "helloworld", [] { simpletest(mk("Hello world"), "Hello world"); },
    "trunc", [] { simpletest(trunc(mk("Hello world"), 3), "Hel"); },
    "padleft", [] { simpletest(padleft(trunc(mk("Hello"), 3), 5), "  Hel");},
    "padright", [] { simpletest(padright(mk("Hel"), 5), "Hel  "); },
    "padcenter", [] { simpletest(padcenter(mk("Hel"), 5), " Hel "); },
    "arrowpad1", [] {
        simpletest(padcenter(mk("Hel"), 10, mk("-->"), mk("<--")),
                   "-->Hel<--<"); },
    "arrowpad2", [] {
        simpletest(padcenter(mk("Hel"), 11, mk("-->"), mk("<--")),
                   ">-->Hel<--<"); },
    "strconcat", [] { simpletest("hello" + space + "world", "hello world");},
    "integers", [] { simpletest(mk(5), "5"); },
    "negint", [] { simpletest(mk(-5), "-5"); },
    "bigint", [] {
        simpletest(mk(0x8000000000000052ul).base(16),
                   "{16}8,000,000,000,000,052");
        simpletest(mk((long)-0x8000000000000000).base(16),
                   "{16}-8,000,000,000,000,000");
        simpletest(mk(0xfffffffffffffffful).base(16),
                   "{16}f,fff,fff,fff,fff,fff"); },
    "negbinint", [] { simpletest(mk(-5).base(2), "{2}-101"); },
    "negtrinint", [] { simpletest(mk(-5).base(3), "{3}-12"); },
    "trinint", [] { simpletest(mk(5).base(3), "{3}12"); },
    "hexint", [] { simpletest(mk(10).base(16), "{16}a"); },
    "HEXint", [] { simpletest(mk(10).base(16).uppercase(), "{16}A"); },
    "unsigned", [] {
        simpletest(mk(0x80000000u).base(16), "{16}80,000,000");
        simpletest(mk((int)0x80000000).base(16), "{16}-80,000,000"); },
    "thousandsep", [] { simpletest(mk(1000), "1,000"); },
    "thousandsep2", [] { simpletest(mk(1234567), "1,234,567"); },
    "binthousandsep", [] { simpletest(mk(72).base(2), "{2}1,001,000"); },
    "hexthousandsep", [] { simpletest(mk(0x123456).base(16),"{16}123,456");},
    "nosep", [] { simpletest(mk(123456).nosep(), "123456"); },
    "complexsep", [] {
        simpletest(mk(123456).sep(fields::mk("ABC"), 1),
                   "1ABC2ABC3ABC4ABC5ABC6");  },
    "hidebase", [] { simpletest(mk(0xaabb).base(16).hidebase(), "a,abb"); },
    "period", [] { simpletest(period, "."); },
    "threads", [] {
        fieldbuf buf1;
        mk("Hello").fmt(buf1);
        assert(!strcmp(buf1.c_str(), "Hello"));
        /* Flushing from another thread shouldn't affect this one. */
        spark<bool> w([] {
                fieldbuf buf2;
                mk("goodbye").fmt(buf2);
                assert(!strcmp(buf2.c_str(), "goodbye"));
                tmpheap::release();
                fieldbuf buf3;
                mk("doomed").fmt(buf3);
                return true; });
        w.get();
        assert(!strcmp(buf1.c_str(), "Hello")); },
    "bigstr", [] {
        char *f = (char *)malloc(1000001);
        memset(f, 'c', 1000000);
        f[1000000] = 0;
        simpletest(mk(f), f);
        free(f);},
    "lotsofbufs", [] {
        fieldbuf buf;
        const field *acc = &mk("");
        for (int i = 0; i < 100; i++) {
            acc = &(*acc + padleft(mk("hello"), 1000)); }
        acc->fmt(buf);
        char reference[1001];
        reference[1000] = 0;
        memset(reference, ' ', 1000);
        memcpy(reference+995, "hello", 5);
        for (int i = 0; i < 100; i++) {
            assert(!memcmp(buf.c_str() + i * 1000,
                           reference,
                           1000)); } },
    "empty", [] { assert(!strcmp(fieldbuf().c_str(), "")); },
    "padnoop", [] { simpletest(padleft(mk("foo"), 1), "foo"); },
    "conc1", [] { simpletest(mk("foo") + "bar", "foobar"); },
    "conc2", [] { simpletest("foo" + mk("bar"), "foobar"); },
    "alwayssign", [] { simpletest(mk(5).alwayssign(), "+5"); },
    "zero", [] { simpletest(mk((long)0), "0"); },
    "double1", [] { simpletest(mk_double(5.0), "5"); },
    "double2", [] { simpletest(mk_double(5.25), "5.25"); },
    "multibufs", [] {
        auto buf1(new fieldbuf());
        auto buf2(new fieldbuf());
        auto buf3(new fieldbuf());
        delete buf2;
        delete buf1;
        delete buf3;},
    "maybe", [] {
        simpletest(mk(maybe<int>(Nothing)), "Nothing");
        simpletest(mk(maybe<int>(91)), "<91>"); },
    "orerror", [] {
        simpletest(mk(orerror<int>(error::disconnected)),
                   "<failed:disconnected>");
        simpletest(mk(orerror<int>(18)), "<18>"); },
    "list", [] {
        simpletest(mk(list<int>()), "{}");
        simpletest(mk(list<int>::mk(1)), "{1}");
        simpletest(mk(list<int>::mk(1, 12)), "{1 12}"); },
    "escape", [] {
        simpletest(mk("foo").escape(), "foo");
        simpletest(mk("foo bar").escape(), "\"foo bar\"");
        simpletest(mk("\"foo bar\"").escape(), "\"\\\"foo bar\\\"\"");
        simpletest(mk("foo\"bar").escape(), "\"foo\\\"bar\"");
        simpletest(mk("foo+bar").escape(), "\"foo+bar\"");
        simpletest(mk("").escape(), "\"\"");
        simpletest(mk("\"").escape(), "\"\\\"\"");
        simpletest(mk("\\").escape(), "\"\\\\\"");
        simpletest(mk("\t").escape(), "\"\\x09\"");
        simpletest(mk("ZZZ\xffXXX").escape(), "\"ZZZ\\xffXXX\"");
        simpletest(mk(7).escape(), "7"); },
    "c_str", [] { simpletest(mk(5), "5"); },
    /* Not really a useful test case, but it makes the coverage 100%,
     * and the print() function's simple enough that just confirming
     * it doesn't crash is good enough. */
    "print", [] { print(mk("hello\n")); });
