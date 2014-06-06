#ifndef LIST_H__
#define LIST_H__

template <typename t>
class list {
    struct member;
    struct nub {
	member *next;
	member *prev;
    };
    nub root;
    list(const list &o) = delete;
    void operator=(const list &o) = delete;
public:
    class iter {
	friend class list;
	member *cursor;
	member *n;
	nub *const eof;
	const bool reverse;
	iter(nub *_eof, bool _reverse);
    public:
	bool finished() const;
	void next();
	void remove();
	t &operator*();
	t *operator->();
	const t &operator*() const;
	const t *operator->() const;
    };
    class const_iter {
	friend class list;
	const member *cursor;
	const nub *const eof;
	const bool reverse;
	const_iter(const nub *_eof, bool _reverse);
    public:
	bool finished() const;
	void next();
	const t &operator*() const;
	const t *operator->() const;
    };

    list();
    ~list();

    /* Iterators: forwards and backwards, const and non-const. */
    iter start();
    const_iter start() const;
    iter reverse();
    const_iter reverse() const;

    /* Check whether a list is currently empty */
    bool empty() const;
    /* Count the number of elements in the list */
    int length() const;
    /* delete everything which is currently in the list */
    void flush();

    /* Simple copying interface to the list, suitable for lists of
     * small items (e.g. pointers). */
    /* Remove the first item from the list and return a copy of it. */
    t pophead();
    /* Remove the last item from the list and return a copy of it. */
    t poptail();
    /* Add a copy of an item to the front of the list */
    void pushhead(const t &);
    /* Add a copy of an item to the end of the list */
    void pushtail(const t &);

    /* Non-copying interface, suitable for more complicated types. */
    /* Allocate a new item, using the default constructor, place it at
       the end of the list, and return a reference to it. */
    t &append();
    /* Allocate a new item, using the default constructor, place it at
       the start of the list, and return a reference to it. */
    t &prepend();

    /* Check that the list looks vaguely reasonable */
    void sanitycheck() const;
};

#endif /* !LIST_H__ */
