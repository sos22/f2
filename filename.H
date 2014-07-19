/* Simple thing to represent filenames.  Basically a wrapper around
 * strings. */
#ifndef FILE_H__
#define FILE_H__

#include "string.H"

class filename;
template <typename> class orerror;

namespace fields {
class field;
const field &mk(const filename &f);
}

class filename {
    friend const fields::field &fields::mk(const filename &);
private: string content;
public:  filename(const string &s);
    /* Add another component to the end of the path, with a separating
       slash.  i.e. filename(x) + "y" = filename("x/y"), not
       filename("xy"). */
public:  filename operator+(const char *) const;
    /* Read the entire file, returning a string.  This is limited to
       only read in relatively small files (a few kilobytes) without
       any zero bytes, and returns an error if asked to read in either
       a large file or one containing nuls. */
public:  orerror<string> readasstring() const;
};

#endif /* !FILE_H__ */
