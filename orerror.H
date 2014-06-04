#ifndef ORERROR_H__
#define ORERROR_H__

#include <assert.h>

#include <utility>

#include "error.H"

template <typename t> class orerror : public obj {
    const bool errored;
    union {
	error err;
	char buf[sizeof(t)];
    };
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
    orerror(error _err)
	: errored(true), err(_err)
	{
	    err = _err;
	}
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
