/* Persistent names for the things which either store data or run
 * tasks.  These differ from peernames in that peernames identify
 * network endpoints, whereas these identify the things behind those
 * endpoints.  i.e. if you give something a new IP address it'll get a
 * new peername but keep its old slavename. */
#ifndef SLAVENAME_H__
#define SLAVENAME_H__

#include "string.H"
#include "wireproto.H"

class slavename;
template <typename> class parser;

namespace fields {
class field;
const field &mk(const slavename &);
};

class slavename {
    friend const fields::field &fields::mk(const slavename &);
private: string content;
private: void operator=(const slavename &) = delete;
private: slavename() = delete;
public:  slavename(const slavename &);
WIREPROTO_WRAPPER_TYPE(slavename, string, public);
};

namespace parsers {
extern const parser< ::slavename> &slavename();
}

#endif /* !SLAVENAME_H__ */
