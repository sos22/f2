#ifndef ORERROR_H__
#define ORERROR_H__

#include <assert.h>

#include <utility>

#include "error.H"

template <typename t> class orerror : public obj {
    explicit orerror(const t &v)
	: errored(false)
	{
	    new (this->buf) t(v);
	}
    explicit orerror(t &&v)
	: errored(false)
	{
	    new (this->buf) t(std::move(v));
	}
    explicit orerror(error _err)
	: errored(true), err(_err)
	{
	    err = _err;
	}
    const bool errored;
    union {
	error err;
	char buf[sizeof(t)];
    };
public:
    ~orerror() {
	if (errored)
	    failure().~error();
	else
	    success().~t();
    }
    static orerror<t> success(t &&x) {
	return orerror(std::move(x));
    }
    static orerror<t> success(const t &x) {
	return orerror(x);
    }
    static orerror<t> failure(error e) {
	return orerror(e);
    }
    const t &success() const {
	assert(!errored);
	return *(const t *)buf;
    }
    error failure() const {
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
