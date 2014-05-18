#ifndef VECTOR_H__
#define VECTOR_H__

#include <stddef.h>

#include "obj.H"

template <typename t> class vector  : public obj {
    unsigned nr;
    unsigned nr_allocated;
    t *content;
    void extend(void);
public:
    vector()
	: nr(0),
	  nr_allocated(0),
	  content(NULL)
	{}
    vector(const vector &o) = delete;
    void operator=(const vector &o) = delete;
    ~vector();
    unsigned size() const { return nr; }
    t &operator[](unsigned idx);
    const t &operator[](unsigned idx) const;
    t *raw() { return content; }
    unsigned push(const t &);
    unsigned push(t &&);
    void remove(unsigned idx);
};

#endif /* !VECTOR_H__ */
