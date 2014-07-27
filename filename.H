/* Simple thing to represent filenames.  Basically a wrapper around
 * strings. */
#ifndef FILE_H__
#define FILE_H__

#include "string.H"

class fd_t;
class filename;
template <typename> class orerror;
template <typename> class parser;

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
public:  filename operator+(const string &) const;
    /* Read the entire file, returning a string.  This is limited to
       only read in relatively small files (a few kilobytes) without
       any zero bytes, and returns an error if asked to read in either
       a large file or one containing nuls. */
public:  orerror<string> readasstring() const;
    /* Read a file and then parse it using some parser.  Similar size
       restrictions to readasstring() */
public:  template<typename t> orerror<t> parse(const parser<t> &) const;
    /* Create a file and populate it with a field.  Returns
     * error::already if the file already exists. */
public:  maybe<error> createfile(const fields::field &) const;
    /* Create an empty file.  The file must not already exist.
     * Returns error::already if the file exists with zero size, or
     * some other error for other error cases. */
public:  maybe<error> createfile() const;
    /* Check whether a file exists and is a file. */
public:  orerror<bool> exists() const;
    /* Open a file in append mode.  The file must already exist. */
public:  orerror<fd_t> openappend() const;
    /* Get the size of a file.  The file must exist. */
public:  orerror<uint64_t> size() const;
    /* Read some bytes out of a file.  start is inclusive, end
       exclusive.  The bounds must be within the file.  If start ==
       end <= filesize then return an empty buffer (or an error). */
public:  orerror<buffer> read(uint64_t start, uint64_t end) const;
    /* Directory iterators.  I'd be happier if diriter were a member
     * method of class filename, but then I'd need to implement a copy
     * constructor, which is a pain. */
public:  class diriter {
    private: orerror<struct __dirstream *> dir;
    private: const char *entry;
    private: void operator=(const diriter &) = delete;
    private: diriter() = delete;
    private: diriter(const diriter &) = delete;
    public:  diriter(const filename &);
    public:  bool isfailure() const;
    public:  error failure() const;
    public:  bool finished() const;
    public:  void next();
        /* Returns strings in the temporary heap. */
    public:  const char *filename() const;
    public:  ~diriter();
    };
    /* Create a directory with this filename, or return an error.
     * Returns error::already if the directory already exists
     * (including if it is non-empty). */
public:  maybe<error> mkdir() const;
    /* Delete a file.  Returns error::already if it doesn't exist, not
     * error::notfound. */
public:  maybe<error> unlink() const;
};

#endif /* !FILE_H__ */
