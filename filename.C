#include "filename.H"

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "fields.H"
#include "orerror.H"

const fields::field &
fields::mk(const filename &f) {
    return "<filename:" + mk(f.content) + ">"; }

filename::filename(const string &s)
    : content(s) {}

filename
filename::operator+(const char *x) const {
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
