#ifndef TID_H__
#define TID_H__

class tid {
    tid(unsigned);
    unsigned val;
public:
    tid() = delete;
    static tid me();
    bool operator==(const tid &) const;
};

#endif /* !TID_H__ */
