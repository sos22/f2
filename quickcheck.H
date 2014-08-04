#ifndef QUICKCHECK_H__
#define QUICKCHECK_H__

template <typename> class maybe;

/* The actual quickcheck type doesn't do a great deal; it's just there
 * to give us something to hang the generator operators off of. */
class quickcheck {
    /* Uniform random over entire range of unsigned numbers, except
     * for trying to pick ``interesting'' numbers a bit more often
     * that it should. */
public: operator unsigned long() const;
public: operator long() const;
public: operator unsigned() const;
public: operator int() const;
public: operator unsigned short() const;
public: operator short() const;
public: operator bool() const;
    /* Cauchy distributed with location 0 and scale 1 */
public: operator double() const;
    /* Returns a string in the quickcheck heap using the entire 8 bit
     * char space, except for nul.  The pointer remains valid until
     * the quickcheck heap is reset.  The strings length is roughly
     * log-uniform up to about 32k, with a preference for numbers near
     * powers of two. */
public: operator const char *() const;
    /* Generate a pathname component.  This consists of printable
     * characters excluding /, is at most 255 characters long, is
     * never empty, and is never . or .. . */
public: const char *filename() const;
    /* Flush the quickcheck heap, releasing all strings (etc)
     * allocated by this thread. */
public: static void flush();
};

#endif /* !QUICKCHECK_H__ */
