#ifndef WIREPROTO_H__
#define WIREPROTO_H__

#include <stdint.h>

#include "error.H"
#include "list.H"
#include "maybe.H"
#include "orerror.H"

class buffer;

namespace wireproto {
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

    class tx_message {
    public:
	const msgtag t;
	tx_message(msgtag);
	maybe<error> serialise(buffer &buf) const;
	template <typename t> tx_message &addparam(parameter<t>, t);
	~tx_message();
    private:
	struct pinstance {
	    uint16_t id;
	    enum {
		p_string,
	    } flavour;
	    union {
		/* utf-8 */
		const char *string;
	    };
	    size_t serialised_size() const;
	    void serialise(buffer &) const;
	};
	list<pinstance> params;
    };
    class rx_message {
	struct idx {
	    uint16_t id;
	    uint16_t offset;
	};
	idx *const index;
	const uint16_t nrparams;
	const size_t payload_size;
	const size_t payload_offset;
	buffer &buf;
	rx_message(msgtag _t,
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
