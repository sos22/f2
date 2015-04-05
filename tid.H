#ifndef TID_H__
#define TID_H__

class tid;

namespace fields {
class field;
const field &mk(const tid &);
}

class tid {
    friend const fields::field &fields::mk(const tid &);
    tid(unsigned);
    unsigned val;
public:
    tid() = delete;
    static tid me();
    unsigned os() const { return val; }
    bool operator==(const tid &) const;
};

#endif /* !TID_H__ */
