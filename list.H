#ifndef LIST_H__
#define LIST_H__

#include <functional>

template <typename> class list;

/* Sorting a linked list is usually a hint that you're doing something
 * wrong, but it's sometimes useful in smaller programs. */
template <typename t> void sort(list<t> &);

template <typename t>
class list {
    friend void sort<t>(list<t> &);
    struct member;
    member *head;
    member *tail;
    void operator=(const list &o) = delete;
public:
    list(const list &o);
    list(list &&o);

    class iter {
        friend class list;
        list *const owner;
        member *cursor;
        const bool reverse;
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
    /* Peek at the first item in the list. */
    const t &peekhead() const;
    /* Add a copy of an item to the front of the list */
    void pushhead(const t &);
    /* Add a copy of an item to the end of the list */
    void pushtail(const t &);

    /* Non-copying interface, suitable for more complicated types. */
    /* Allocate a new item, using the default constructor, place it at
       the end of the list, and return a reference to it. */
    template <typename... args> t &append(args...);
    /* Allocate a new item, using the default constructor, place it at
       the start of the list, and return a reference to it. */
    template <typename... args> t &prepend(args...);

    /* Check that the list looks vaguely reasonable */
    void sanitycheck() const;

    /* Create a new list and initialise it by running a mapping
       function over this list. */
    template <typename other_t> list<other_t> map(
        std::function<other_t (const t&)> what) const;
};

#endif /* !LIST_H__ */
