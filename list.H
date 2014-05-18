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
	nub *eof;
	iter(member *_cursor,
	     member *_n,
	     nub *_eof);
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
	const member *n;
	const nub *eof;
	const_iter(const member *_cursor,
		   const member *_n,
		   const nub *_eof);
    public:
	bool finished() const;
	void next();
	const t &operator*() const;
	const t *operator->() const;
    };

    list();
    ~list();
    iter start();
    const_iter start() const;
    bool empty() const;
    void flush();
    t pop();
    void push(const t &);
    t *tail();
    t *head();
    t *append();
    t *prepend();
};

#endif /* !LIST_H__ */
