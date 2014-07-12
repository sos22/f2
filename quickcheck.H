#ifndef QUICKCHECK_H__
#define QUICKCHECK_H__

/* The actual quickcheck type doesn't do a great deal; it's just there
 * to give us something to hang the generator operators off of. */
class quickcheck {
    /* Uniform random over entire range of unsigned numbers, except
     * for trying to pick ``interesting'' numbers a bit more often
     * that it should. */
public: operator unsigned() const;
public: operator unsigned short() const;
    /* Cauchy distributed with location 0 and scale 1 */
public: operator double() const;
    /* Returns a string in the quickcheck heap using the 8 bit char
     * space, except for nul.  The pointer remains valid until the
     * quickcheck heap is reset. */
public: operator const char *() const;
    /* Flush the quickcheck heap, releasing all strings (etc)
     * allocated by this thread. */
public: static void flush();
};

#endif /* !QUICKCHECK_H__ */
