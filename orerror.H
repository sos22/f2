#ifndef ORERROR_H__
#define ORERROR_H__

#include <assert.h>

#include <utility>

#include "error.H"

template <typename t> class orerror {
    const bool errored;
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
            err = _err;
        }
    void operator=(const orerror &o) {
        if (isfailure()) {
            if (o.isfailure()) {
                err = o.failure();
            } else {
                failure().~error();
                new (this->buf) t(o.success()); }
        } else {
            if (o.isfailure()) {
                success().~t();
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
        return err;
    }
    bool issuccess() const {
        return !errored;
    }
    bool isfailure() const {
        return errored;
    }
};

#endif /* !ORERROR_H__ */
