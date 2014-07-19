#ifndef WIREPROTO_H__
#define WIREPROTO_H__

#include <stddef.h>
#include <stdint.h>

#include "error.H"
#include "list.H"
#include "maybe.H"
#include "orerror.H"
#include "quickcheck.H"

class buffer;

namespace wireproto {
class bufslice;
template <typename typ> maybe<typ> deserialise(bufslice &slice);
class msgtag;
template <typename t> class parameter;
class rx_message;
class rx_messagestatus;
class sequencenr;
class sequencerstatus;
};

namespace fields {
class field;
const field &mk(const wireproto::msgtag &);
const field &mk(const wireproto::rx_message *);
const field &mk(const wireproto::rx_messagestatus &);
const field &mk(const wireproto::sequencenr &);
const field &mk(const wireproto::sequencerstatus &);
template <typename t> const field &mk(const ::maybe<t> &);
template <typename t> const field &mk(const wireproto::parameter<t> &);
};

#define WIREPROTO_TYPE(name)                                            \
    public: void addparam(wireproto::parameter<name>,                   \
                          wireproto::tx_message &) const;               \
    public: static maybe<name> fromcompound(const wireproto::rx_message &)

#define WIREPROTO_WRAPPER_TYPE(wrapper, wrapped)                        \
    friend class wireproto::tx_message;                                 \
    friend class wireproto::rx_message;                                 \
    public: static wrapper __fromwrapped__(wrapped t);                  \
    private: wrapped unwrap() const;                                    \
    WIREPROTO_TYPE(wrapper)

namespace wireproto {
    struct bufslice;

    /* Must be POD or serialise/deserialise won't work. */
    struct sequencenr {
        friend const fields::field &fields::mk(const wireproto::sequencenr &t);
    private:
        friend class sequencer;
        uint64_t val;
        explicit sequencenr(uint64_t _val)
            : val(_val)
            {}
    public:
        static const sequencenr invalid;
        bool operator ==(const sequencenr &o) const
            { return val == o.val; }
        sequencenr reply() const
            { return sequencenr(val | (1ul << 63)); }
        bool isreply() const {
            return !!(val & (1ul << 63)); }
    };
    class sequencerstatus {
        WIREPROTO_WRAPPER_TYPE(sequencerstatus, uint64_t);
    public:  uint64_t nextseq;
    public:  sequencerstatus(uint64_t _nextseq)
        : nextseq(_nextseq) {}
    public:  sequencerstatus(const quickcheck &q);
    public:  bool operator==(const sequencerstatus &o) const {
        return nextseq == o.nextseq; }
    };
    class sequencer {
        uint64_t last;
    public:
        sequencer() : last(0) {}
        sequencenr get() {
            last++;
            return sequencenr(last);
        }
    public: typedef sequencerstatus status_t;
    public: status_t status() const { return status_t(last+1); }
    };
    class msgtag {
        friend const class fields::field &fields::mk(const msgtag &);
        uint16_t val;
        WIREPROTO_WRAPPER_TYPE(msgtag, uint16_t);
    public:
        explicit msgtag(uint16_t _val)
            : val(_val)
            {}

        bool operator==(const msgtag &o) const {
            return val == o.val;
        }
        bool operator!=(const msgtag &o) const {
            return !(*this == o);
        }
    };
    template <typename _instanceT>
    class parameter {
        friend class tx_message;
        friend class rx_message;
        friend const fields::field &fields::mk<>(const parameter &);
        uint16_t id;
    public: typedef _instanceT instanceT;
    public: explicit parameter(uint16_t _id)
            : id(_id) {}

        /* because I can't figure out how to make parameter<int> be a
           friend of parameter<long> */
    public: uint16_t ___get_id___() const { return id; }

    public: template<typename o> explicit parameter(parameter<o> _o)
            : id(_o.___get_id___())
            {}
    };

    struct index {
    public: uint16_t id;
    public: uint16_t offset; /* From start of message, including
                              * header and index. */
    public: index(uint16_t _id, uint16_t _offset)
        : id(_id), offset(_offset) {}
    };
    /* Arbitrary limit on size of individual messages, just to avoid
       stupid overflow bugs and DoS attacks. */
    static const unsigned MAXMSGSIZE = 10000000;
    /* Similar limit on number of parameters per message. */
    static const unsigned MAXMSGPARAMS = 1000;
    struct wireheader {
    public: uint32_t sz; /* bytes, limited to MAXMSGSIZE */
    public: uint16_t nrparams; /* count, limited to MAXMSGPARAMS */
    public: msgtag tag;
    public: sequencenr seq;
    public: index idx[];
    public: wireheader(uint32_t _sz,
                       uint16_t _nrparams,
                       msgtag _tag,
                       sequencenr _seq)
        : sz(_sz), nrparams(_nrparams), tag(_tag), seq(_seq) {}
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
        virtual void serialise(buffer &buf) const;
    protected:
        void serialise(buffer &buf, sequencenr snr) const;
    public: size_t serialised_size() const;
    private:
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
                    unsigned sz;
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
        void serialise(buffer &buf) const;
        template <typename t> req_message &addparam(parameter<t> p, t v)
            { tx_message::addparam(p,v); return *this; }
    };
    class resp_message : public tx_message {
    public:
        const sequencenr sequence;
        resp_message(const rx_message &r);
        void serialise(buffer &buf) const;
    };
    class err_resp_message : public tx_message {
    public:
        const sequencenr sequence;
        err_resp_message(const rx_message &r, const error &e);
        void serialise(buffer &buf) const;
    };
    class rx_messagestatus {
        WIREPROTO_WRAPPER_TYPE(rx_messagestatus, msgtag);
    public: msgtag t;
    public: explicit rx_messagestatus(const msgtag &_t)
        : t(_t) {}
    public: rx_messagestatus()
        : t(0) {}
    public: bool operator==(const rx_messagestatus &o) const {
        return o.t == t; }
    };
    class rx_message {
        friend const fields::field &fields::mk(const wireproto::rx_message *);
    private: rx_message() = delete;
    public:  rx_message(const rx_message &o);
    public:  rx_message(rx_message &&o);
    private: void operator=(const rx_message &) = delete;
    private: const wireheader *msg;
    private: bool owning; /* free() msg when we're done with it */
    private: rx_message(const wireheader *, bool);
    private: static orerror<rx_message> parse(const wireheader *,
                                              size_t sz,
                                              bool);
    public:  static orerror<rx_message> fetch(const bufslice &buf);
    public:  static orerror<rx_message> fetch(buffer &buf);
    public:  template <typename typ> maybe<typ> getparam(parameter<typ>) const;
    public:  template <typename typ> maybe<error> fetch(
        parameter<typ>, typ &out) const;
    public:  template <typename typ> maybe<error> fetch(
        parameter<list<typ> >, list<typ> &out) const;
    public:  rx_message *steal();
    public:  bool isreply() const;
    public:  sequencenr sequence() const;
    public:  msgtag tag() const;
    public:  typedef rx_messagestatus status_t;
    public:  status_t status() const;
    public:  ~rx_message();
    };

    class tx_compoundparameter {
        friend class tx_message::pinstance;
        friend class tx_message;
        tx_message *const content;
        tx_compoundparameter *clone() const {
            return new tx_compoundparameter(content);
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

    /* Special parameter whose presence indicates that the message
       represents an error.  rx_message will return an error and
       discard the message if this is present.  If present, this must
       be the only parameter. */
    extern const parameter<error> err_parameter;
};

namespace tests {
void wireproto();
}

#endif /* !WIREPROTO_H__ */
