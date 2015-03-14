#ifndef LIST_H__
#define LIST_H__

#include <functional>

class deserialise1;
class quickcheck;
template <typename> class list;
template <typename> class nnp;
template <typename> class parser;
class serialise1;

/* Sorting a linked list is usually a hint that you're doing something
 * wrong, but it's sometimes useful in smaller programs. */
template <typename t> void sort(list<t> &);
/* Same thing with an explicit > operator. */
template <typename t> void sort(
    list<t> &, const std::function<bool (const t&, const t&)> &);

/* Tag type to select a constructor */
class Immediate {};

template <typename t>
class list {
    friend void sort<t>(list<t> &,
                        const std::function<bool (const t&, const t&)> &);
    struct member;
    member *head;
    member *tail;
public:
    void operator=(const list &o);
    list(const list &o);
    list(list &&o);
    list(const quickcheck &);

    template <typename ... args> static list mk(args...);

    template <typename ... args> list(Immediate, args...params)
        : head(NULL),
          tail(NULL) {
        pushtail(std::forward<args>(params)...); }

    explicit list(deserialise1 &);
    void serialise(serialise1 &) const;

    list steal();

    class iter {
        friend class list;
        list *owner;
        member *cursor;
        bool reverse;
        iter(list *owner, const bool reverse);
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
        const list *const owner;
        const member *cursor;
        const bool reverse;
        const_iter(const list *owner, const bool reverse);
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
    /* Check for list emptiness with fewer sanity checks */
    bool empty_unsafe() const;
    /* Count the number of elements in the list */
    unsigned length() const;
    /* delete everything which is currently in the list */
    void flush();
    /* Transfer everything from @a to the end of this list, emptying
     * @a in the process. */
    void transfer(list &a);

    /* Simple copying interface to the list, suitable for lists of
     * small items (e.g. pointers). */
    /* Remove the first item from the list and return a copy of it. */
    t pophead();
    /* Remove the first element and discard it. */
    void drophead();
    /* Remove the last item from the list and return a copy of it. */
    t poptail();
    /* Peek at the first item in the list. */
    const t &peekhead() const;
    t &peekhead();
    /* Add a copy of an item to the front of the list */
    void pushhead(const t &);
    /* Copy some items to the tail of the list, in order */
    /* Not the same as append, despite the similar prototypes: this
     * pushes a sequence of t's taken from its varargs, whereas append
     * constructs a single t from its varargs. */
    void pushtail();
    template <typename ... args> void pushtail(const t &, args &&...);

    /* Remove an element from the list by value.  It must be present
     * precisely once, and the type must have an == operator. */
    void drop(const t &);
    /* Remove an element from the list by pointer.  We don't
     * dereference the argument; it's just used to find the list
     * element.  The referrent must be in the list. */
    void drop(const t *);

    /* Grab a reference to an element of the list given its zero-based
       index.  The index must be within bounds. */
    t &idx(unsigned);
    const t &idx(unsigned) const;

    /* Non-copying interface, suitable for more complicated types. */
    /* Allocate a new item, using the default constructor, place it at
       the end of the list, and return a reference to it. */
    template <typename... args> t &append(args &&...);
    /* Allocate a new item, using the default constructor, place it at
       the start of the list, and return a reference to it. */
    template <typename... args> t &prepend(args && ...);

    /* O(n) membership test. */
    bool contains(const t&) const;

    /* Check that the list looks vaguely reasonable */
    void sanitycheck() const;

    /* Check a list for duplicates with an O(n^2) algorithm.  Only
       really useful in test programs, because it's too slow for real
       work. */
    bool hasdupes() const;

    /* Create a new list and initialise it by running a mapping
       function over this list. */
    template <typename other_t> list<other_t> map(
        std::function<other_t (const t&)> what) const;

    bool operator==(const list &o) const;

    /* A scheme for allocating a list entry before hooking it into the
     * list.  Occasionally useful to make the synchronisation work out
     * a bit better. */
public: class partial : private member {
        friend t &list::pushtail(nnp<partial>);
        /* Would be private if I could figure out the friend
         * declarations. */
    public:  template <typename ... args> explicit partial(args && ...);
    public:  t &val();
    public:  const t &val() const;
    private: void operator=(const partial &) = delete;
    private: partial(const partial &) = delete; };
public: template <typename ... args> static nnp<partial> mkpartial(args &&...);
public: t &pushtail(nnp<partial>);

public: static const parser<list<t> > &parse(const parser<t> &);
};

template <typename t, typename ... args> list<t> mklist(const t &, args &&...);

namespace tests { void _list(); }

#endif /* !LIST_H__ */
