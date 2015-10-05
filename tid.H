#ifndef TID_H__
#define TID_H__

namespace fields { class field; }

class tid {
    explicit tid(unsigned);
    unsigned val;
public:
    tid() = delete;
    static tid me();
    unsigned os() const { return val; }
    bool operator==(const tid &) const;
public: const fields::field &field() const;
};

#endif /* !TID_H__ */
