#ifndef ORERROR_H__
#define ORERROR_H__

#include <assert.h>

#include <utility>

#include "error.H"

template <typename t> class orerror {
    bool errored;
    union {
        error err;
        char buf[sizeof(t)];
    };
    orerror() = delete;
public:
    orerror(const t &v)
        : errored(false)
        {
            new (this->buf) t(v);
        }
    orerror(t &&v)
        : errored(false)
        {
            new (this->buf) t(std::move(v));
        }
    orerror(const error &_err)
        : errored(true), err(_err)
        {
        }
    void operator=(const orerror &o) {
        if (isfailure()) {
            if (o.isfailure()) {
                err = o.failure();
            } else {
                failure().~error();
                errored = false;
                new (this->buf) t(o.success()); }
        } else {
            if (o.isfailure()) {
                success().~t();
                errored = true;
                new (&this->err) error(o.failure());
            } else {
                success() = o.success(); } } }
    ~orerror() {
        if (errored)
            failure().~error();
        else
            success().~t();
    }
    const t &success() const {
        assert(!errored);
        return *(const t *)buf;
    }
    t &success() {
        assert(!errored);
        return *(t *)buf;
    }
    const error &failure() const {
        assert(errored);
        return err;
    }
    bool issuccess() const {
        return !errored;
    }
    bool isfailure() const {
        return errored;
    }

    template <typename o> orerror(const orerror<o> &other)
        : errored(other.isfailure()) {
        if (errored) {
            new (&err) error(other.failure());
        } else {
            new (buf) t(other.success()); } }
};

#endif /* !ORERROR_H__ */
