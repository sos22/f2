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
#include "orerror.tmpl"
#include "parsers.tmpl"
#include "test.tmpl"

tests::event<orerror<void> *> testhooks::filename::readasbufferloop;
tests::event<ssize_t *> testhooks::filename::createfileloop;
tests::event<struct dirent **> testhooks::filename::diriterevt;

const fields::field &
fields::mk(const filename &f) {
    return "<filename:" + mk(f.content).escape() + ">"; }

filename::filename(const char *s)
    : content(s) {}

filename::filename(deserialise1 &ds)
    : content(ds) {}

filename::filename(const quickcheck &q)
    : content(q.filename()) {}

filename
filename::operator+(const string &x) const {
    return filename(content + "/" + x); }

bool
filename::operator==(const filename &o) const {
    return content == o.content; }

orerror<buffer>
filename::readasbuffer() const {
    orerror<buffer> res(Success);
    int fd(open(content.c_str(), O_RDONLY));
    if (fd < 0) {
        if (errno == ENOENT) res = error::notfound;
        else res = error::from_errno();
        return res; }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        ::close(fd);
        res = error::from_errno();
        return res; }
    if ((st.st_mode & S_IFMT) != S_IFREG) {
        ::close(fd);
        res = error::from_errno(ESPIPE);
        return res; }
    while (true) {
        /* Arbitrarily read whole-file readin to 10MB to prevent
         * someone doing something stupid. */
        if (bytecount::bytes(res.success().avail()) >= 10_MB) {
            close(fd);
            res = error::overflowed;
            return res; }
        /* Local file IO is assumed to be fast, so no clientio
         * token. */
        auto r(res.success().receive(clientio::CLIENTIO, fd_t(fd)));
        testhooks::filename::readasbufferloop.trigger(&r);
        if (r.issuccess()) continue;
        close(fd);
        if (r != error::disconnected) res = r.failure();
        return res; } }

orerror<string>
filename::readasstring() const {
    auto r(readasbuffer());
    if (r.isfailure()) return r.failure();
    auto &b(r.success());
    b.queue("\0", 1);
    const char *cstr = (const char *)b.linearise(0, b.avail());
    if (memchr(cstr, '\0', b.avail()) != cstr + b.avail() - 1) {
        return error::noparse; }
    return string(cstr); }

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
        testhooks::filename::createfileloop.trigger(&this_time);
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

orerror<fd_t>
filename::openro() const {
    int fd(::open(content.c_str(), O_RDONLY));
    if (fd < 0) return error::from_errno();
    else return fd_t(fd); }

orerror<bytecount>
filename::size() const {
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) return error::from_errno();
    else if (!S_ISREG(st.st_mode)) return error::notafile;
    else {
        assert(st.st_size >= 0);
        return bytecount::bytes(st.st_size); } }

orerror<buffer>
filename::read(bytecount start, bytecount end) const {
    assert(end >= start);
    auto need((end - start).just());
    orerror<buffer> res(Success);
    auto _fd(::open(content.c_str(), O_RDONLY));
    if (_fd < 0) return error::from_errno();
    if (start != 0_B) {
        assert(start.b <= INT64_MAX);
        auto r(::lseek(_fd, (off_t)start.b, SEEK_SET));
        if (r < 0) {
            res = error::from_errno();
            return res; }
        else if ((uint64_t)r != start.b) {
            res = error::pastend;
            return res; } }
    fd_t fd(_fd);
    while (res.success().avail() < need.b) {
        /* IO to local files doesn't really need a clientio token. */
        auto r(res.success().receive(clientio::CLIENTIO,
                                     fd,
                                     Nothing,
                                     need.b - res.success().avail()));
        if (r.isfailure()) {
            ::close(_fd);
            res = r.failure();
            return res; } }
    ::close(_fd);
    return res; }

filename::diriter::diriter(const class filename &f)
    : dir((DIR *)0xf001ul) {
    DIR *d = opendir(f.content.c_str());
    if (d == NULL) {
        if (errno == ENOENT) dir = error::notfound;
        else dir = error::from_errno();
        entry = NULL; }
    else {
        dir = d;
        next(); } }

fd_t
filename::diriter::dirfd() const {
    return fd_t(::dirfd(dir.fatal("dirfd on failed dir"))); }

bool
filename::diriter::isfailure() const {
    return dir.isfailure(); }

error
filename::diriter::failure() const {
    return dir.failure(); }

bool
filename::diriter::finished() const {
    return dir.isfailure() || dir.success() == NULL; }

void
filename::diriter::next() {
    assert(!finished());
    int e(errno);
    errno = 0;
    auto de(::readdir(dir.success()));
    while (de != NULL &&
           (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)) {
        de = ::readdir(dir.success()); }
    testhooks::filename::diriterevt.trigger(&de);
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
filename::rmtree() const {
    filename::diriter di(*this);
    while (!di.isfailure() && !di.finished()) {
        if (strcmp(di.filename(), ".") != 0 &&
            strcmp(di.filename(),"..") != 0) {
            auto e((*this + string(di.filename())).rmtree());
            if (e.isfailure()) return e.failure(); }
        di.next(); }
    if (di.isfailure()) {
        if (di.failure() == error::notfound) return error::already;
        else if (di.failure() != error::from_errno(ENOTDIR)) {
            return di.failure(); } }
    auto e(unlink());
    if (e == error::from_errno(EISDIR)) return rmdir();
    else return e; }

orerror<void>
filename::unlink() const {
    int r(::unlink(content.c_str()));
    if (r == 0) return Success;
    else if (errno == ENOENT) return error::already;
    else return error::from_errno(); }

orerror<void>
filename::rename(const filename &o) const {
    if (::rename(content.c_str(), o.content.c_str()) < 0) {
        return error::from_errno(); }
    else return Success; }

orerror<bool>
filename::contenteq(const filename &o) const {
    auto us(readasbuffer());
    if (us.isfailure()) return us.failure();
    auto them(o.readasbuffer());
    if (them.isfailure()) return them.failure();
    return us.success().contenteq(them.success()); }

orerror<filename>
filename::mktemp() {
    orerror<filename> res(Success, "");
    unsigned cntr = 0;
    while (true) {
        res.mksuccess(("tmp/tmp" + fields::mk(cntr)).c_str());
        auto r(res.success().isfile());
        if (r.isfailure()) {
            if (r == error::from_errno(EISDIR) ||
                r == error::notafile) {
                r = true; }
            else {
                res = r.failure();
                break; } }
        if (r == false) break;
        assert(r == true);
        cntr++; }
    return res; }

const parser<filename> &
filename::parser() {
    return ("<filename:" + string::parser() + ">")
        .map<filename>([] (const string &x) { return filename(x); }); }

unsigned long
filename::hash() const { return content.hash(); }

const fields::field &
filename::field() const { return fields::mk(*this); }

void
filename::serialise(serialise1 &s) const { s.push(content); }
