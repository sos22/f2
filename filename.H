/* Simple thing to represent filenames.  Basically a wrapper around
 * strings. */
#ifndef FILE_H__
#define FILE_H__

#include "orerror.H"
#include "string.H"

class buffer;
class bytecount;
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
public:  filename(const char *s);
public:  explicit filename(const string &_content) : content(_content) {}
    /* Quickcheck filenames are single component i.e. no / character. */
public:  filename(const quickcheck &q);
    /* Add another component to the end of the path, with a separating
       slash.  i.e. filename(x) + "y" = filename("x/y"), not
       filename("xy"). */
public:  filename operator+(const string &) const;
    /* Compare two filenames for equality.  No attempt is made to
     * normalise the filenames, so ./x/../y != ./y. */
public:  bool operator==(const filename &o) const;
    /* Read the entire file, returning a string.  This is limited to
       only read in relatively small files (a few kilobytes) without
       any zero bytes, and returns an error if asked to read in either
       a large file or one containing nuls. */
public:  orerror<string> readasstring() const;
    /* Read a file and then parse it using some parser.  Similar size
       restrictions to readasstring() */
public:  template<typename t> orerror<t> parse(const parser<t> &) const;
    /* Create a file and populate it by copying from something in
     * memory.  Returns error::already if the file already exists. */
public:  orerror<void> createfile(const void *data, bytecount datasize) const;
    /* Create a file and populate it with a field.  Returns
     * error::already if the file already exists. */
public:  orerror<void> createfile(const fields::field &) const;
    /* Create a file and populate it with the contents of a buffer.
     * Returns error::already if the file already exists. */
public:  orerror<void> createfile(const buffer &buf) const;
    /* Create an empty file.  The file must not already exist.
     * Returns error::already if the file exists with zero size, or
     * some other error for other error cases. */
public:  orerror<void> createfile() const;
    /* Check whether a file exists and is a file. */
public:  orerror<bool> isfile() const;
    /* Check whether a file exists and is a directory. */
public:  orerror<bool> isdir() const;
    /* Open a file in append mode.  The file must already exist and
     * contain precisely @size bytes.  If the file is too small fail
     * with error::toosoon; if it's too big, fail with
     * error::toolate. */
public:  orerror<fd_t> openappend(bytecount size) const;
    /* Get the size of a file.  The file must exist. */
public:  orerror<bytecount> size() const;
    /* Read some bytes out of a file.  start is inclusive, end
     * exclusive.  The bounds must be within the file.  If start ==
     * end <= filesize then return an empty buffer (or an error). */
public:  orerror<buffer> read(bytecount start, bytecount end) const;
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
        /* Returns strings in the temporary heap.  The string does not
         * include the directory name. */
    public:  const char *filename() const;
    public:  ~diriter();
    };
    /* Create a directory with this filename, or return an error.
     * Returns error::already if the directory already exists
     * (including if it is non-empty). */
public:  orerror<void> mkdir() const;
    /* Remove a directory.  Fails and returns error::notempty if the
     * directory is not empty. */
public:  orerror<void> rmdir() const;
    /* Delete a file.  Returns error::already if it doesn't exist, not
     * error::notfound. */
public:  orerror<void> unlink() const;

    /* Convert to a simple string. */
public:  const string &str() const { return content; }

    /* Convert to a field. */
public:  const fields::field &field() const;
};

template <typename> class parser;
namespace parsers {
const parser<filename> &_filename();
}

namespace tests {
void _filename();
}

#endif /* !FILE_H__ */
