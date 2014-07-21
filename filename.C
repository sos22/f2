#include "filename.H"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "fields.H"
#include "logging.H"
#include "orerror.H"

const fields::field &
fields::mk(const filename &f) {
    return "<filename:" + mk(f.content) + ">"; }

filename::filename(const string &s)
    : content(s) {}

filename
filename::operator+(const string &x) const {
    return filename(content + "/" + x); }

orerror<string>
filename::readasstring() const {
    int fd(open(content.c_str(), O_RDONLY));
    if (fd < 0) return error::from_errno();
    off_t sz(lseek(fd, 0, SEEK_END));
    if (sz < 0 || lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return error::from_errno(); }
    /* Arbitrarily read whole-file readin to 10MB to prevent someone
       doing something stupid. */
    if (sz > 10000000l) {
        close(fd);
        return error::overflowed; }
    char *buf = (char *)malloc(sz + 1);
    off_t off;
    ssize_t thistime;
    for (off = 0; off < sz; off += thistime) {
        thistime = read(fd, buf + off, sz - off);
        if (thistime < 0) {
            close(fd);
            free(buf);
            return error::from_errno(); } }
    close(fd);
    if (memchr(buf, '\0', sz) != NULL) {
        free(buf);
        return error::noparse; }
    return string::steal(buf); }

maybe<error>
filename::createfile(const fields::field &f) const {
    fields::fieldbuf buf;
    f.fmt(buf);
    auto c(buf.c_str());
    auto s(strlen(c));
    if (s >= 1000000000) return error::overflowed;
    int fd(::open(content.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600));
    if (fd < 0) {
        if (errno == EEXIST) return error::already;
        else return error::from_errno(); }
    off_t off;
    ssize_t this_time;
    for (off = 0; off < (ssize_t)s; off += this_time) {
        this_time = write(fd, c + off, s - off);
        if (this_time <= 0) {
            auto r(this_time == 0
                   ? error::truncated
                   : error::from_errno());
            close(fd);
            auto rr(unlink());
            if (rr.isjust()) {
                /* Can't really do much here. */
                logmsg(loglevel::error,
                       "cannot unlink newly-created file " +
                       fields::mk(*this) +
                       ": " +
                       fields::mk(rr.just())); }
            return r; } }
    close(fd);
    return Nothing; }

maybe<error>
filename::createfile() const {
    int fd(::open(content.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600));
    if (fd >= 0) {
        ::close(fd);
        return Nothing; }
    if (errno != EEXIST) return error::from_errno();
    /* Empty files get special treatment. */
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) return error::from_errno();
    else if (S_ISREG(st.st_mode) && st.st_size == 0) return error::already;
    else return error::from_errno(EEXIST); }

orerror<bool>
filename::exists() const {
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) {
        if (errno == ENOENT) return false;
        else return error::from_errno();
    } else if (S_ISDIR(st.st_mode)) return error::from_errno(EISDIR);
    else if (S_ISCHR(st.st_mode) ||
             S_ISBLK(st.st_mode) ||
             S_ISFIFO(st.st_mode) ||
             S_ISSOCK(st.st_mode)) return error::notafile;
    else if (!S_ISREG(st.st_mode)) return error::from_errno(EINVAL);
    else return true; }

maybe<error>
filename::mkdir() const {
    int r(::mkdir(content.c_str(), 0700));
    if (r == 0) return Nothing;
    assert(r < 0);
    if (errno != EEXIST) return error::from_errno();
    struct stat st;
    if (::stat(content.c_str(), &st) < 0) return error::from_errno();
    if (S_ISDIR(st.st_mode)) return error::already;
    else return error::from_errno(EEXIST); }

maybe<error>
filename::unlink() const {
    int r(::unlink(content.c_str()));
    if (r == 0) return Nothing;
    else if (errno == ENOENT) return error::already;
    else return error::from_errno(); }
