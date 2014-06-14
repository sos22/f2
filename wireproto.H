#ifndef WIREPROTO_H__
#define WIREPROTO_H__

#include <stddef.h>
#include <stdint.h>

#include "error.H"
#include "list.H"
#include "maybe.H"
#include "orerror.H"

class buffer;

namespace wireproto {
class msgtag;
class rx_message;
template <typename t> class parameter;
};

namespace fields {
class field;
const field &mk(const wireproto::msgtag &);
const field &mk(const wireproto::rx_message *);
template <typename t> const field &mk(const ::maybe<t> &);
template <typename t> const field &mk(const wireproto::parameter<t> &);
};

namespace wireproto {
    struct bufslice;

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
        friend const class fields::field &fields::mk(const msgtag &);
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
        bool operator!=(const msgtag &o) const {
            return val != o.val;
        }
    };
    template <typename _instanceT>
    class parameter {
        friend class tx_message;
        friend class rx_message;
        friend const fields::field &fields::mk<>(const parameter &);
        uint16_t id;
        const char *description;
    public:
        typedef _instanceT instanceT;
        explicit parameter(int _id, const char *_description)
            : id(_id), description(_description)
            {}

        /* because I can't figure out how to make parameter<int> be a
           friend of parameter<long> */
        uint16_t ___get_id___() const { return id; }

        template<typename o> explicit parameter(parameter<o> _o)
            : id(_o.___get_id___())
            {}
    };

    class rx_message;
    class tx_compoundparameter;
    class tx_message {
        friend class tx_compoundparameter;
        tx_message() : t(-1), params()  {}
        tx_message(const tx_message &o) = delete;
        tx_message &addparam(uint16_t, const void *, size_t);
    public:
        const msgtag t;
        tx_message(msgtag);
        template <typename t> tx_message &addparam(parameter<t>, const t &);
        template <typename t> tx_message &addparam(parameter<list<t> >,
                                                   const list<t> &);
        tx_message &addparam(parameter<const char *>, const char *);
        virtual ~tx_message();
        virtual maybe<error> serialise(buffer &buf) const;
    protected:
        maybe<error> serialise(buffer &buf, sequencenr snr) const;
    private:
        size_t serialised_size() const;
        tx_message *clone() const;
        struct pinstance {
            uint16_t id;
            enum {
                p_internal = 91,
                p_external,
                p_compound,
            } flavour;
            union {
                struct {
                    unsigned char sz;
                    char content[15];
                } internal;
                struct {
                    /* make field order different between internal and
                       external so as to catch mistakes where we use
                       the wrong one. */
                    void *content; /* malloc()ed owned pointer */
                    unsigned short sz;
                } external;
                tx_compoundparameter *compound;
            };
            ~pinstance();
            pinstance(const pinstance &o);
            pinstance();
            size_t serialised_size() const;
            void serialise(buffer &) const;
            pinstance clone() const;
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
    class err_resp_message : public tx_message {
    public:
        const sequencenr sequence;
        err_resp_message(const rx_message &r, const error &e);
        /* This automatically logs a message when it fails.  Most
           callers will ignore the return value; it's only there to
           make the virtual overloading work. */
        maybe<error> serialise(buffer &buf) const;
    };
    class rx_message {
        friend class rx_compoundparameter;
        friend const fields::field &fields::mk(const rx_message *);
        struct idx {
            uint16_t id;
            uint16_t offset;
        };
        const idx *const index;
        enum flavour_t {
            fl_buffer,
            fl_slice,
            fl_clone
        };
        const flavour_t flavour;
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
                   buffer &_buf,
                   const idx *_index);
        rx_message(msgtag _t,
                   sequencenr _sequence,
                   uint16_t _nrparams,
                   size_t _payload_size,
                   size_t _payload_offset,
                   buffer &_buf);
        rx_message(msgtag _t,
                   sequencenr _sequence,
                   uint16_t _nrparams,
                   size_t _payload_size,
                   size_t _payload_offset,
                   buffer &_buf,
                   const idx *_index,
                   bool);
    public:
        const msgtag t;
        static orerror<const rx_message *> fetch(const bufslice &buf);
        static orerror<const rx_message *> fetch(buffer &buf);
        template <typename typ> maybe<typ> getparam(parameter<typ>) const;
        template <typename typ> maybe<error> fetch(
            parameter<typ>, typ &out) const;
        template <typename typ> maybe<error> fetch(
            parameter<list<typ> >, list<typ> &out) const;
        void finish() const;
        rx_message *clone() const;
    };

    class tx_compoundparameter {
        friend class tx_message::pinstance;
        friend class tx_message;
        tx_message *content;
        tx_compoundparameter *clone() const {
            return new tx_compoundparameter(content->clone());
        }
        size_t serialised_size() const {
            return content->serialised_size();
        }
        tx_compoundparameter(const tx_compoundparameter &) = delete;
        void operator=(const tx_compoundparameter &) = delete;
    public:
        tx_compoundparameter(tx_message *_content = NULL)
            : content(_content
                      ? _content->clone()
                      : new tx_message())
        {}
        ~tx_compoundparameter()
            {
                delete content;
            }
        template <typename t> tx_compoundparameter &addparam(
            parameter<t> tmpl, const t &val) {
            content->addparam(tmpl, val);
            return *this;
        }
    };
    class rx_compoundparameter {
    public:
        const rx_message *content;
    private:
        void operator=(const rx_compoundparameter &) = delete;
        rx_compoundparameter(const rx_message *_content)
            : content(_content)
            {}
    public:
        rx_compoundparameter()
            : content(NULL)
            {}
        rx_compoundparameter(const rx_compoundparameter &o)
            : content(o.content->clone())
            {}
        static orerror<rx_compoundparameter *> fetch(const bufslice &o);
        ~rx_compoundparameter() { content->finish(); }
        template <typename t> const maybe<t> getparam(
            parameter<t> tmpl) const {
            return content->getparam(tmpl);
        }
    };

    template <typename t> const fields::field &paramfield(
        const rx_message &msg, const parameter<t> &param);

    /* Special parameter whose presence indicates that the message
       represents an error.  rx_message will return an error and
       discard the message if this is present.  If present, this must
       be the only parameter. */
    extern const parameter<error> err_parameter;
};

#define WIREPROTO_TYPE(name)                                            \
    void addparam(wireproto::parameter<name>, wireproto::tx_message &) const; \
    static maybe<name> getparam(wireproto::parameter<name> p,           \
                                const wireproto::rx_message &)

#endif /* !WIREPROTO_H__ */
