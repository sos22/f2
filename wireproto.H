#ifndef WIREPROTO_H__
#define WIREPROTO_H__

#include <stdint.h>

#include "error.H"
#include "list.H"
#include "maybe.H"
#include "orerror.H"

class buffer;
class controlclient;

namespace wireproto {
    /* Must be POD or serialise/deserialise won't work. */
    struct sequencenr {
    private:
	friend class sequencer;
	uint16_t val;
	explicit sequencenr(uint16_t _val)
	    : val(_val)
	    {}
    public:
	static const sequencenr invalid;
	bool operator ==(const sequencenr &o) const
	    { return val == o.val; }
	sequencenr reply() const
	    { return sequencenr(val | 0x8000); }
    };
    class sequencer {
	uint16_t last;
    public:
	sequencer() : last(0) {}
	sequencenr get() {
	    /* XXX overflow */
	    last++;
	    return sequencenr(last);
	}
	void put(sequencenr) {
	}
    };
    class msgtag {
	friend class tx_message;
	friend class rx_message;
	uint16_t val;
    public:
	explicit msgtag(int _val)
	    : val(_val)
	    {}

	bool operator==(const msgtag &o) const {
	    return val == o.val;
	}
    };
    template <typename _instanceT>
    class parameter {
	friend class tx_message;
	friend class rx_message;
	uint16_t id;
    public:
	typedef _instanceT instanceT;
	explicit parameter(int _id)
	    : id(_id)
	    {}
    };

    class rx_message;
    class tx_message {
    public:
	const msgtag t;
	tx_message(msgtag);
	template <typename t> tx_message &addparam(parameter<t>, t);
	~tx_message();
	virtual maybe<error> serialise(buffer &buf) const;
    protected:
	maybe<error> serialise(buffer &buf, sequencenr snr) const;
    private:
	struct pinstance {
	    uint16_t id;
	    enum {
		p_string = 91,
		p_int32,
	    } flavour;
	    union {
		/* utf-8 */
		const char *string;
		int32_t int32;
	    };
	    size_t serialised_size() const;
	    void serialise(buffer &) const;
	};
	list<pinstance> params;
    };
    class req_message : public tx_message {
    public:
	const sequencenr sequence;
	req_message(msgtag, sequencenr snr);
	maybe<error> serialise(buffer &buf) const;
	template <typename t> req_message &addparam(parameter<t> p, t v)
	    { tx_message::addparam(p,v); return *this; }
    };
    class resp_message : public tx_message {
    public:
	const sequencenr sequence;
	resp_message(const rx_message &r);
	maybe<error> serialise(buffer &buf) const;
    };
    class rx_message {
	struct idx {
	    uint16_t id;
	    uint16_t offset;
	};
	idx *const index;
    public:
	const sequencenr sequence;
    private:
	const uint16_t nrparams;
	const size_t payload_size;
	const size_t payload_offset;
	buffer &buf;
	rx_message(msgtag _t,
		   sequencenr _sequence,
		   uint16_t _nrparams,
		   size_t _payload_size,
		   size_t _payload_offset,
		   buffer &_buf);
    public:
	const msgtag t;
	static maybe<const rx_message *> fetch(buffer &buf);
	template <typename typ> maybe<typ> getparam(parameter<typ>) const;
	void finish() const;
    };
};

#endif /* !WIREPROTO_H__ */
