#include "bytecount.H"

#include "parsers.H"

#include "parsers.tmpl"

const ::parser<bytecount> &
bytecount::parser() {
    class f : public ::parser<bytecount> {
    public: orerror<result> parse(const char *what) const {
        auto i(parsers::intparser<unsigned long>().parse(what));
        if (i.isfailure()) return i.failure();
        auto j(i.success());
        auto res(j.res);
        auto l(j.left);
        if (!strncmp(l, "B", 1)) {
            return result(l + 1, bytecount::bytes(res)); }
        else if (!strncmp(l, "kB", 2)) {
            return result(l + 2, bytecount::kilobytes(res)); }
        else if (!strncmp(l, "MB", 2)) {
            return result(l + 2, bytecount::megabytes(res)); }
        else if (!strncmp(l, "kiB", 3)) {
            return result(l + 3, bytecount::kibibytes(res)); }
        else if (!strncmp(l, "MiB", 3)) {
            return result(l + 3, bytecount::mebibytes(res)); }
        else return result(l, bytecount::bytes(res)); } };
    return *new f(); }
