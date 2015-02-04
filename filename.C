#include "filename.H"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "buffer.H"
#include "bytecount.H"
#include "fd.H"
#include "fields.H"
#include "logging.H"
#include "orerror.H"
#include "parsers.H"
#include "quickcheck.H"
#include "test.H"

#include "list.tmpl"
#include "parsers.tmpl"
#include "test.tmpl"

static tests::event<ssize_t *> readasstringloop;
static tests::event<ssize_t *> createfileloop;
static tests::event<struct dirent **> diriterevt;

const fields::field &
fields::mk(const filename &f) {
    return "<filename:" + mk(f.content).escape() + ">"; }

filename::filename(const char *s)
    : content(s) {}

filename::filename(const quickcheck &q)
    : content(q.filename()) {}

filename
filename::operator+(const string &x) const {
    return filename(content + "/" + x); }

bool
filename::operator==(const filename &o) const {
    return content == o.content; }

orerror<string>
filename::readasstring() const {
    int fd(open(content.c_str(), O_RDONLY));
    if (fd < 0) return error::from_errno();
    off_t _sz(lseek(fd, 0, SEEK_END));
    if (_sz < 0 || lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return error::from_errno(); }
    size_t sz((size_t)_sz);
    /* Arbitrarily read whole-file readin to 10MB to prevent someone
       doing something stupid. */
    if (sz > 10000000) {
        close(fd);
        return error::overflowed; }
    char *buf = (char *)calloc(sz + 1, 1);
    unsigned long off;
    size_t thistime;
    for (off = 0; off < sz; off += thistime) {
        ssize_t _thistime(::read(fd, buf + off, sz - off));
        readasstringloop.trigger(&_thistime);
        if (_thistime < 0) {
            close(fd);
            free(buf);
            return error::from_errno(); }
        if (_thistime == 0) return error::pastend;
        thistime = (size_t)_thistime; }
    close(fd);
    if (memchr(buf, '\0', sz) != NULL) {
        free(buf);
        return error::noparse; }
    return string::steal(buf); }

orerror<void>
filename::createfile(const void *c, bytecount s) const {
    int fd(::open(content.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600));
    if (fd < 0) {
        if (errno == EEXIST) return error::already;
        else return error::from_errno(); }
    size_t off;
    ssize_t this_time;
    for (off = 0; off < s.b; off += (size_t)this_time) {
        this_time = ::write(fd, (const void *)((uintptr_t)c + off), s.b - off);
        createfileloop.trigger(&this_time);
        if (this_time <= 0) {
            auto r(this_time == 0
                   ? error::truncated
                   : error::from_errno());
            close(fd);
            auto rr(unlink());
            if (rr.isfailure()) {
                /* Can't really do much here. */
                rr.warn("cannot unlink newly-created file " +
                        fields::mk(*this)); }
            return r; } }
    close(fd);
    return Success; }

orerror<void>
filename::createfile(const fields::field &f) const {
    fields::fieldbuf buf;
    f.fmt(buf);
    auto c(buf.c_str());
    size_t s(strlen(c));
    if (s >= 1000000000) return error::overflowed;
    return createfile(c, bytecount::bytes(s)); }

orerror<void>
filename::createfile(const buffer &b) const {
    return createfile(b.linearise(b.offset(), b.offset() + b.avail()),
                      bytecount::bytes(b.avail())); }

orerror<void>
filename::createfile() const {
    int fd(::open(content.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600));
    if (fd >= 0) {
        ::close(fd);
        return Success; }
    if (errno != EEXIST) return error::from_errno();
    /* Empty files get special treatment. */
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) return error::from_errno();
    else if (S_ISREG(st.st_mode) && st.st_size == 0) return error::already;
    else return error::from_errno(EEXIST); }

orerror<void>
filename::replace(const buffer &buf) const {
    filename tmpfile(*this);
    tmpfile.content += "T";
    while (true) {
        auto r(tmpfile.createfile(buf));
        if (r.issuccess()) break;
        else if (r != error::already) return r;
        tmpfile.content += "T"; }
    auto r(::rename(tmpfile.content.c_str(), content.c_str()));
    if (r < 0) {
        auto e(error::from_errno());
        tmpfile.unlink()
            .warn("removing temporary file " + tmpfile.field());
        return e; }
    else return Success; }

orerror<bool>
filename::isfile() const {
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) {
        if (errno == ENOENT) return false;
        else return error::from_errno(); }
    else if (S_ISDIR(st.st_mode)) return error::from_errno(EISDIR);
    else if (S_ISCHR(st.st_mode) ||
             S_ISBLK(st.st_mode) ||
             S_ISFIFO(st.st_mode) ||
             S_ISSOCK(st.st_mode)) return error::notafile;
    else if (!S_ISREG(st.st_mode)) return error::from_errno(EINVAL);
    else return true; }

orerror<bool>
filename::isdir() const {
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) {
        if (errno == ENOENT) return false;
        else return error::from_errno(); }
    else if (S_ISDIR(st.st_mode)) return true;
    else if (S_ISREG(st.st_mode) ||
             S_ISCHR(st.st_mode) ||
             S_ISBLK(st.st_mode) ||
             S_ISFIFO(st.st_mode) ||
             S_ISSOCK(st.st_mode)) return error::notadir;
    else return error::from_errno(EINVAL); }

orerror<fd_t>
filename::openappend(bytecount oldsize) const {
    int fd(::open(content.c_str(), O_WRONLY | O_APPEND));
    struct stat stbuf;
    if (fd < 0) return error::from_errno();
    else if (::fstat(fd, &stbuf) < 0) {
        auto e(error::from_errno());
        ::close(fd);
        return e; }
    else if (oldsize != bytecount::bytes(stbuf.st_size)) {
        ::close(fd);
        if (oldsize > bytecount::bytes(stbuf.st_size)) return error::toosoon;
        else return error::toolate; }
    else return fd_t(fd); }

orerror<bytecount>
filename::size() const {
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) return error::from_errno();
    assert(st.st_size >= 0);
    return bytecount::bytes(st.st_size); }

orerror<buffer>
filename::read(bytecount start, bytecount end) const {
    assert(end >= start);
    auto need((end - start).just());
    auto _fd(::open(content.c_str(), O_RDONLY));
    if (_fd < 0) return error::from_errno();
    if (start != 0_B) {
        assert(start.b <= INT64_MAX);
        auto r(::lseek(_fd, (off_t)start.b, SEEK_SET));
        if (r < 0) return error::from_errno();
        else if ((uint64_t)r != start.b) return error::pastend; }
    fd_t fd(_fd);
    buffer res;
    while (res.avail() < need.b) {
        /* IO to local files doesn't really need a clientio token. */
        auto r(res.receive(clientio::CLIENTIO,
                           fd,
                           Nothing,
                           need.b - res.avail()));
        if (r.isfailure()) {
            ::close(_fd);
            return r.failure(); } }
    ::close(_fd);
    return res.steal(); }

filename::diriter::diriter(const class filename &f)
    : dir((DIR *)0xf001ul) {
    DIR *d = opendir(f.content.c_str());
    if (d == NULL) {
        dir = error::from_errno();
        entry = NULL; }
    else {
        dir = d;
        next(); } }

bool
filename::diriter::isfailure() const {
    return dir.isfailure(); }

error
filename::diriter::failure() const {
    return dir.failure(); }

bool
filename::diriter::finished() const {
    return dir.success() == NULL; }

void
filename::diriter::next() {
    int e(errno);
    errno = 0;
    auto de(::readdir(dir.success()));
    diriterevt.trigger(&de);
    if (de == NULL) {
        closedir(dir.success());
        if (errno == 0) dir = NULL;
        else dir = error::from_errno();
        errno = e; }
    else {
        assert(errno == 0);
        errno = e;
        entry = tmpheap::strdup(de->d_name); } }

const char *
filename::diriter::filename() const {
    assert(!finished());
    return entry; }

filename::diriter::~diriter() {
    if (dir.issuccess() && dir.success() != NULL) closedir(dir.success()); }

orerror<void>
filename::mkdir() const {
    int r(::mkdir(content.c_str(), 0700));
    if (r == 0) return Success;
    assert(r < 0);
    if (errno != EEXIST) return error::from_errno();
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) return error::from_errno();
    if (S_ISDIR(st.st_mode)) return error::already;
    else return error::from_errno(EEXIST); }

orerror<void>
filename::rmdir() const {
    int r(::rmdir(content.c_str()));
    if (r == 0) return Success;
    else if (errno == ENOTEMPTY) return error::notempty;
    else return error::from_errno(); }

orerror<void>
filename::unlink() const {
    int r(::unlink(content.c_str()));
    if (r == 0) return Success;
    else if (errno == ENOENT) return error::already;
    else return error::from_errno(); }

const parser<filename> &
parsers::_filename() {
    return ("<filename:" + strparser + ">")
        .map<filename>([] (const char *x) { return filename(string(x)); }); }

const fields::field &
filename::field() const { return fields::mk(*this); }

/* Basic functionality tests.  A lot of these are more to make sure
   that the behaviour doesn't change unexpectedly, rather than to
   check that the current behaviour is actually desirable. */
void
tests::_filename() {
    testcaseV("filename", "parser", [] {
            parsers::roundtrip(parsers::_filename()); });
    testcaseV("filename", "basics", [] {
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
                assert(r.length() == 3);
                assert(r.idx(0) == ".");
                assert(r.idx(1) == "..");
                assert(r.idx(2) == "bar"); }
            assert(foo.rmdir() == error::notempty);
            assert((foo + "bar").unlink().issuccess());
            assert(foo.rmdir().issuccess());
        });
    testcaseV("filename", "fifo", [] {
            filename foo2("foo2");
            foo2.unlink();
            if(mknod("foo2",0600|S_IFIFO,0)<0)error::from_errno().fatal("foo2");
            assert(foo2.isfile() == error::notafile);
            assert(foo2.rmdir() == error::from_errno(ENOTDIR));
            filename::diriter it(foo2);
            assert(it.isfailure());
            assert(it.failure() == error::from_errno(ENOTDIR));
            assert(foo2.unlink().issuccess()); });
    testcaseV("filename", "bigfile", [] {
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
            assert(foo3.unlink().issuccess()); });
    testcaseV("filename", "readempty", [] {
            filename foo("foo");
            foo.unlink();
            foo.createfile(fields::mk("ABCD")).fatal("cannot make foo");
            auto r(foo.read(2_B, 2_B)
                   .fatal("empty read"));
            assert(r.empty());
            foo.unlink().fatal("unlinking foo"); });
    testcaseV("filename", "rodir", [] {
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
            dir.rmdir().fatal("rm foo4"); });
#if TESTING
    testcaseV("filename", "errinject", [] {
            {   eventwaiter<ssize_t *> w(
                    readasstringloop, [] (ssize_t *what) {
                        errno = ETXTBSY;
                        *what = -1; });
                filename foo("foo");
                foo.unlink();
                foo.createfile(fields::mk(7)).fatal("create foo");
                assert(foo.readasstring() == error::from_errno(ETXTBSY));
                foo.unlink().fatal("unlink foo"); }
            {   eventwaiter<ssize_t *> w(
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
                filename::diriter it(f);
                assert(!it.isfailure());
                eventwaiter<struct dirent **> w(
                    diriterevt, [] (struct dirent **d) {
                        errno = ETXTBSY;
                        *d = NULL; });
                assert(!it.isfailure());
                it.next();
                assert(it.isfailure());
                assert(it.failure() == error::from_errno(ETXTBSY)); } } );
#endif
    testcaseV("filename", "str", [] {
            assert((filename("foo") + "bar").str() ==
                   string("foo/bar")); });
}
