#include <sys/stat.h>
#include <unistd.h>

#include "bytecount.H"
#include "filename.H"
#include "test.H"
#include "test2.H"

#include "parsers.tmpl"
#include "test.tmpl"
#include "test2.tmpl"

/* Basic functionality tests.  A lot of these are more to make sure
 * that the behaviour doesn't change unexpectedly, rather than to
 * check that the current behaviour is actually desirable. */
static testmodule __testfilename(
    "filename",
    list<filename>::mk("filename.C", "filename.H"),
    testmodule::LineCoverage(75_pc),
    testmodule::BranchCoverage(50_pc),
    "parser", [] { parsers::roundtrip<filename>(); },
    "basics", [] {
        filename foo("foo");
        assert(foo + "bar" == filename("foo/bar"));
        foo.unlink();
        (foo + "bar").unlink();
        foo.rmdir();
        assert(foo.isfile() == false);
        assert(!foo
               .createfile(fields::mk(5))
               .isfailure());
        assert(foo.createfile(fields::mk(5)) == error::already);
        assert(foo.isfile() == true);
        assert(foo.readasstring() == string("5"));
        assert(foo.size() == 1_B);
        {   auto r(foo.read(0_B,1_B).fatal("read foo"));
            assert(r.avail() == 1);
            assert(r.offset() == 0);
            assert(r.idx(0) == '5'); }
        assert(foo.read(1_B,2_B) == error::disconnected);
        assert(foo.createfile() == error::from_errno(EEXIST));
        assert(filename("/dev/tty").readasstring() ==
               error::from_errno(ESPIPE));
        assert(foo.unlink().issuccess());
        assert(foo.unlink() == error::already);
        assert(foo.createfile().issuccess());
        assert(foo.createfile() == error::already);
        assert(foo.mkdir() == error::from_errno(EEXIST));
        assert(foo.unlink().issuccess());
        assert(foo.isfile() == false);
        assert(foo.mkdir().issuccess());
        assert(foo.mkdir() == error::already);
        assert(foo.isfile() == error::from_errno(EISDIR));
        assert((foo + "bar").createfile().issuccess());
        {   list<string> r;
            for (filename::diriter it(foo); !it.finished(); it.next()) {
                r.pushtail(string(it.filename())); }
            sort(r);
            assert(r.length() == 1);
            assert(r.idx(0) == "bar"); }
        assert(foo.rmdir() == error::notempty);
        assert((foo + "bar").unlink().issuccess());
        assert(foo.rmdir().issuccess()); },
    "fifo", [] {
        filename foo2("foo2");
        foo2.unlink();
        if (mknod("foo2",0600|S_IFIFO,0) < 0) error::from_errno().fatal("foo2");
        assert(foo2.isfile() == error::notafile);
        assert(foo2.rmdir() == error::from_errno(ENOTDIR));
        filename::diriter it(foo2);
        assert(it.isfailure());
        assert(it.failure() == error::from_errno(ENOTDIR));
        assert(foo2.unlink().issuccess()); },
    "bigfile", [] {
        filename foo3("foo3");
        foo3.unlink();
        assert(foo3.createfile(fields::mk("ABCD")).issuccess());
        {   auto r(foo3.openappend(4_B).fatal("openappend"));
            unsigned char buf[8192];
            memset(buf, 'Z', 8192);
            for (unsigned long x = 0; x < 10000000; x += sizeof(buf)) {
                assert(r.write(clientio::CLIENTIO, buf, sizeof(buf))
                       == sizeof(buf)); }
            r.close(); }
        {   auto r(foo3.read(1_B, 5_B).fatal("readbig"));
            assert(r.avail() == 4);
            assert(r.offset() == 0);
            assert(r.idx(0) == 'B');
            assert(r.idx(1) == 'C');
            assert(r.idx(2) == 'D');
            assert(r.idx(3) == 'Z'); }
        assert(foo3.readasstring() == error::overflowed);
        assert(foo3.unlink().issuccess());
        assert(foo3.createfile().issuccess());
        {   auto r(foo3.openappend(0_B).fatal("openappend"));
            assert(r.write(clientio::CLIENTIO, "AB\0CD", 5) == 5);
            r.close(); }
        assert(foo3.readasstring() == error::noparse);
        assert(foo3.unlink().issuccess()); },
    "readempty", [] {
        filename foo("foo");
        foo.unlink();
        foo.createfile(fields::mk("ABCD")).fatal("cannot make foo");
        auto r(foo.read(2_B, 2_B).fatal("empty read"));
        assert(r.empty());
        foo.unlink().fatal("unlinking foo"); },
    "rodir", [] {
        filename dir("foo4");
        dir.rmdir();
        auto file(dir + "bar");
        dir.mkdir().fatal("foo4");
        if (chmod("foo4", 0) < 0) error::from_errno().fatal("chmod");
        assert(file.createfile() == error::from_errno(EACCES));
        assert(file.createfile(fields::mk(73)) ==
               error::from_errno(EACCES));
        assert(file.isfile() == error::from_errno(EACCES));
        assert(file.unlink() == error::from_errno(EACCES));
        dir.rmdir().fatal("rm foo4"); },
#if TESTING
    "errinject", [] {
        using namespace testhooks::filename;
        {   tests::eventwaiter<orerror<void> *> w(
                readasbufferloop, [] (orerror<void> *what) {
                    *what = error::from_errno(ETXTBSY); });
            filename foo("foo");
            foo.unlink();
            foo.createfile(fields::mk(7)).fatal("create foo");
            assert(foo.readasstring() == error::from_errno(ETXTBSY));
            foo.unlink().fatal("unlink foo"); }
        {   tests::eventwaiter<ssize_t *> w(
                createfileloop, [] (ssize_t *what) {
                    filename("foo").unlink().fatal("badness");
                    errno = ETXTBSY;
                    *what = -1; });
            filename foo("foo");
            foo.unlink();
            assert(foo.createfile(fields::mk(73)) ==
                   error::from_errno(ETXTBSY)); }
        {   filename f("foo");
            f.mkdir().fatal("mkdir foo");
            (f + "bar").createfile().fatal("create bar");
            filename::diriter it(f);
            assert(!it.isfailure());
            {   tests::eventwaiter<struct dirent **> w(
                    diriterevt, [] (struct dirent **d) {
                        errno = ETXTBSY;
                        *d = NULL; });
                assert(!it.isfailure());
                it.next();
                assert(it.isfailure());
                assert(it.failure() == error::from_errno(ETXTBSY)); }
            f.rmtree().fatal("removing foo"); } },
#endif
    "str", [] { assert((filename("foo") + "bar").str() == string("foo/bar")); },
    "iterbad", [] {
        filename::diriter it(filename("does not exist"));
        assert(it.finished());
        assert(it.isfailure());
        assert(it.failure() == error::notfound); },
    "iter..", [] {
        quickcheck q;
        auto t(filename::mktemp(q).fatal("mktemp"));
        t.mkdir().fatal("mkdir");
        assert(!filename::diriter(t).isfailure());
        assert(filename::diriter(t).finished());
        (t + "foo").createfile();
        filename::diriter it(t);
        for ( ; !it.finished(); it.next()) {
            assert(strcmp(it.filename(), "foo") == 0); }
        assert(!it.isfailure());
        t.rmtree().fatal("removing " + t.field()); },
    "mktemp", [] {
        /* Should be able to create a few hundred temporary files
         * without any collisions. */
        list<filename> created;
        quickcheck q;
        for (unsigned x = 0; x < 200; x++) {
            auto f(filename::mktemp(q).fatal("creating temp " + fields::mk(x)));
            assert(!f.isdir().fatal("isdir"));
            assert(!f.isfile().fatal("isdir"));
            if ((bool)q) f.createfile().fatal("createfile");
            else f.mkdir().fatal("mkdir");
            created.append(f); }
        for (auto it(created.start()); !it.finished(); it.next()) {
            auto e(it->rmdir());
            if (e == error::from_errno(ENOTDIR)) e = it->unlink();
            e.fatal("removing " + it->field()); } });
