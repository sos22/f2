#include "buffer.H"

#include <string.h>

#include "either.H"
#include "fd.H"
#include "proto.H"
#include "pubsub.H"
#include "spark.H"
#include "test.H"
#include "timedelta.H"

#include "list.tmpl"
#include "spark.tmpl"
#include "wireproto.tmpl"

wireproto_wrapper_type(buffer::status_t)
void
buffer::status_t::addparam(
    wireproto::parameter<buffer::status_t> tmpl,
    wireproto::tx_message &out) const {
    out.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
        wireproto::tx_compoundparameter()
        .addparam(proto::bufferstatus::prod, prod)
        .addparam(proto::bufferstatus::cons, cons)); }
maybe<buffer::status_t>
buffer::status_t::fromcompound(const wireproto::rx_message &msg) {
#define doparam(name) auto name(msg.getparam(proto::bufferstatus::name))
    doparam(prod);
    doparam(cons);
#undef doparam
    if (!prod || !cons) return Nothing;
    else return buffer::status_t(prod.just(), cons.just()); }
const fields::field &
fields::mk(const buffer::status_t &o) {
    return "<prod:" + mk(o.prod) +
        " cons:" + mk(o.cons) +
        ">"; }

buffer::subbuf *
buffer::newsubbuf(size_t sz, bool middle, bool atstart, bool insert)
{
    subbuf *res;
    size_t newsz;
    for (newsz = 16384; newsz < sz + sizeof(*res); newsz *= 2)
        ;
    res = (subbuf *)malloc(newsz);
    new (res) subbuf();
    res->sz = newsz - sizeof(*res);
    if (middle)
        res->prod = (res->sz + sz) / 2;
    else
        res->prod = 0;
    res->cons = res->prod;
    if (insert) {
        if (atstart) {
            res->next = first;
            first = res;
            if (!last)
                last = res;
        } else {
            res->next = NULL;
            if (last)
                last->next = res;
            else
                first = res;
            last = res;
        }
    }
    return res;
}

maybe<error>
buffer::receive(clientio io, fd_t fd, maybe<timestamp> deadline)
{
    subbuf *b;
    bool unlinked;
    if (last && last->sz - last->prod >= 4096) {
        b = last;
        unlinked = false;
    } else {
        b = unlinked_subbuf(0);
        unlinked = true; }
    auto read(fd.read(io, b->payload + b->prod, b->sz - b->prod, deadline));
    if (read.isfailure()) {
        if (unlinked) free(b);
        return read.failure(); }
    if (unlinked) {
        b->next = NULL;
        if (last) last->next = b;
        else first = b;
        last = b; }
    b->prod += read.success();
    prod += read.success();
    assert(b->cons != b->prod);
    return Nothing;
}

orerror<subscriptionbase *>
buffer::receive(clientio io,
                fd_t fd,
                subscriber &sub,
                maybe<timestamp> deadline) {
    {   iosubscription ios(io, sub, fd.poll(POLLIN));
        auto r(sub.wait(deadline));
        if (r == NULL) return error::timeout;
        if (r != &ios) return r; }
    auto r(receive(io, fd, deadline));
    if (r.isjust()) return r.just();
    else return NULL; }


orerror<subscriptionbase *>
buffer::send(clientio io,
             fd_t fd,
             subscriber &sub,
             maybe<timestamp> deadline) {
    {   iosubscription ios(io, sub, fd.poll(POLLOUT));
        auto r(sub.wait(deadline));
        if (r == NULL) return error::timeout;
        if (r != &ios) return r; }
    assert(first);
    assert(first->prod != first->cons);
    auto wrote(fd.write(io,
                        first->payload + first->cons,
                        first->prod - first->cons,
                        deadline));
    if (wrote.isfailure())
        return wrote.failure();
    first->cons += wrote.success();
    if (first->cons == first->prod) {
        auto b = first;
        first = b->next;
        free(b);
        if (!first) last = NULL; }
    cons += wrote.success();
    return NULL; }

void
buffer::queue(const void *buf, size_t sz)
{
    auto tl(last);
    if (!tl || tl->sz - tl->prod < sz)
        tl = extend_end(sz);
    memcpy(tl->payload + tl->prod, buf, sz);
    tl->prod += sz;
    assert(tl->prod > tl->cons);
    prod += sz;
}

bool
buffer::empty() const
{
    return first == NULL;
}

buffer::~buffer(void)
{
    auto i(first);
    auto n(first);
    while (i) {
        assert(i->cons < i->prod);
        n = i->next;
        free(i);
        i = n;
    }
}

size_t
buffer::avail() const
{
    size_t ack;
    ack = 0;
    for (auto it(first); it; it = it->next) {
        assert(it->cons < it->prod);
        ack += it->prod - it->cons; }
    return ack;
}

void
buffer::fetch(void *buf, size_t sz)
{
    cons += sz;
    while (1) {
        if (sz == 0)
            return;
        /* Caller should check that there's enough stuff available before
           calling. */
        assert(first);
        assert(first->cons < first->prod);
        if (sz < first->prod - first->cons) {
            if (buf)
                memcpy(buf, first->payload + first->cons, sz);
            first->cons += sz;
            return;
        }

        if (buf) {
            memcpy(buf,
                   first->payload + first->cons,
                   first->prod - first->cons);
            buf = (void *)((unsigned long)buf + first->prod - first->cons);
        }
        sz -= first->prod - first->cons;
        auto n(first->next);
        free(first);
        first = n;
        if (!first) {
            assert(sz == 0);
            last = NULL; }
    }
}

void
buffer::pushback(const void *what, size_t sz)
{
    assert(cons >= sz);
    if (!sz) return;
    auto b(first);
    assert(!b || b->cons < b->prod);
    if (!(b && b->cons >= sz)) {
        b = extend_start(sz);
        assert(b->cons <= b->sz);
    }
    assert(b);
    assert(b->cons <= b->sz);
    assert(b->cons >= sz);
    b->cons -= sz;
    memcpy((void *)((unsigned long)b->payload + b->cons), what, sz);
    cons -= sz;
}

size_t
buffer::offset() const
{
    return cons;
}

void
buffer::discard(size_t sz)
{
    fetch(NULL, sz);
}

char
buffer::idx(size_t off) const
{
    off -= cons;
    auto it(first);
    while (1) {
        assert(it);
        assert(it->cons < it->prod);
        if (off < it->prod - it->cons)
            return it->payload[it->cons + off];
        off -= it->prod - it->cons;
        it = it->next;
    }
}

const void *
buffer::linearise(size_t start, size_t end)
{
    subbuf *prev;
    assert(start >= cons);
    assert(end <= prod);
    assert(start <= end);
    if (start == end) {
        /* Caller asked for an empty buffer.  It doesn't matter what
           we give them, so just use a static buffer to make our lives
           a bit easier. */
        static unsigned char b;
        return &b; }
    start -= cons;
    end -= cons;
    prev = NULL;
    auto it(first);
    while (1) {
        assert(it);
        assert(it->cons < it->prod);
        if (start >= it->prod - it->cons) {
            start -= it->prod - it->cons;
            end -= it->prod - it->cons;
            prev = it;
            it = prev->next;
            continue;
        }
        if (end <= it->prod - it->cons)
            return it->payload + start + it->cons;
        break;
    }
    /* Going to have to actually linearise. */
    assert(it != last);
    assert(it->next != NULL);
    auto b(unlinked_subbuf(end - start));
    memcpy(b->payload,
           it->payload + it->cons + start,
           it->prod - it->cons - start);
    b->prod = it->prod - it->cons - start;
    assert(b->cons == 0);
    assert(b->sz >= end - start);
    if (start == 0) {
        /* Remove @it from list completely and replace it with b*/
        if (prev) {
            prev->next = b;
        } else {
            first = b; }
        b->next = it->next;
        free(it);
    } else {
        /* Trim the end of @it */
        it->prod = it->cons + start;
        auto n(it->next);
        b->next = n;
        it->next = b;
    }
    assert(b->next != first);
    prev = b;
    it = prev->next;
    end -= start;
    assert(it != first);
    while (b->prod < end) {
        assert(it != first);
        assert(it);
        if (it->prod - it->cons <= end - b->prod) {
            memcpy(b->payload + b->prod,
                   it->payload + it->cons,
                   it->prod - it->cons);
            b->prod += it->prod - it->cons;
            if (it == last) {
                last = prev; }
            prev->next = it->next;
            free(it);
            it = prev->next;
        } else {
            memcpy(b->payload + b->prod,
                   it->payload + it->cons,
                   end - b->prod);
            it->cons += end - b->prod;
            b->prod = end;
        }
    }
    if (!first) last = NULL;
    return b->payload;
}

buffer::status_t
buffer::status() const {
    return status_t(prod, cons); }

void
tests::buffer(void)
{
    testcaseS(
        "buffer",
        "fuzz",
        [] (support &t) {
            static const int nr_iters = 10;
            for (int i = 0; i < nr_iters; i++) {
                t.msg("iteration %d/%d", i, nr_iters);
                auto b = new ::buffer();

                char *content;
                size_t size;
                long prod;
                long cons;

                int cntr;

                content = NULL;
                size = 0;
                prod = 0;
                cons = 0;
                cntr = 0;

                for (int j = 0; j < 1000; j++) {
                    assert(b->empty() == (size == 0));
                    assert(b->avail() == size);
                    assert(b->offset() == (unsigned long)cons);
                    assert(size == (size_t)(prod - cons));
                    switch (random() % 6) {
                    case 0: {
                        size_t sz = random() % 65536;
                        t.detail(" queue %zd", sz);
                        content = (char *)realloc(content, size + sz);
                        for (unsigned k = 0; k < sz; k++)
                            content[k + size] = cntr++;
                        b->queue(content + size, sz);
                        size += sz;
                        prod += sz;
                        break;
                    }
                    case 1: {
                        if (!size)
                            continue;
                        size_t sz = random() % size;
                        t.detail(" fetch %zd", sz);
                        void *buffer = malloc(sz);
                        b->fetch(buffer, sz);
                        cons += sz;
                        for (unsigned k = 0; k < sz; k++)
                            assert( ((char *)buffer)[k] == (char)content[k] );
                        memmove(content, content + sz, size - sz);
                        size -= sz;
                        free(buffer);
                        break;
                    }
                    case 2: {
                        if (!cons)
                            continue;
                        size_t sz = random() % cons;
                        t.detail(" pushback %zd", sz);
                        content = (char *)realloc(content, size + sz);
                        memmove(content + sz, content, size);
                        for (unsigned k = 0; k < sz; k++)
                            content[k] = cntr++;
                        b->pushback(content, sz);
                        cons -= sz;
                        size += sz;
                        break;
                    }
                    case 3: {
                        if (!size)
                            continue;
                        size_t sz = random() % size;
                        t.detail(" discard %zd", sz);
                        b->discard(sz);
                        memmove(content, content + sz, size - sz);
                        cons += sz;
                        size -= sz;
                        break;
                    }
                    case 4: {
                        if (size <= 1)
                            continue;
                        size_t start = cons + (random() % (size - 1));
                        size_t end = start + (random() % (prod - start));
                        t.detail(" linearise %zd %zd", start, end);
                        const void *buf = b->linearise(start, end);
                        assert(buf);
                        for (unsigned k = 0; k < end - start; k++)
                            assert( ((char *)buf)[k] == content[k+start-cons]);
                        break;
                    }
                    case 5: {
                        if (!size)
                            continue;
                        size_t off = cons + (random() % size);
                        t.detail(" idx %zd", off);
                        assert(b->idx(off) == content[off - cons]);
                        break;
                    }
                    }
                }
                free(content);
                delete b;
            }
        });

    /* A couple more test cases to get to 100% coverage */
    testcaseV("buffer", "status", [] () {
            ::buffer buf;
            buf.queue("Hello", 5);
            char b[3];
            buf.fetch(b, 3);
            auto s(buf.status());
            assert(s.prod == 5);
            assert(s.cons == 3);
            fields::fieldbuf fb;
            fields::mk(s).fmt(fb);
            assert(!strcmp(fb.c_str(), "<prod:5 cons:3>")); } );

    testcaseV("buffer", "statuswire", [] () {
            ::buffer buf1;
            buf1.queue("Hello", 5);
            char b[3];
            buf1.fetch(b, 3);
            auto s(buf1.status());
            wireproto::parameter<buffer::status_t> param(7);
            wireproto::msgtag tag(99);
            {   ::buffer buf2;
                wireproto::tx_message(tag).serialise(buf2);
                auto r(wireproto::rx_message::fetch(buf2));
                assert(r.issuccess());
                assert(r.success().getparam(param) == Nothing); }
            {   ::buffer buf2;
                wireproto::tx_message(tag).addparam(param,s).serialise(buf2);
                auto r(wireproto::rx_message::fetch(buf2));
                assert(r.issuccess());
                assert(r.success().getparam(param).isjust());
                assert(r.success().getparam(param).just().cons == s.cons);
                assert(r.success().getparam(param).just().prod == s.prod); }});

    testcaseV("buffer", "pushbackempty", [] () {
            ::buffer buf;
            buf.queue("Hello", 5);
            char b[5];
            buf.fetch(b, 5);
            buf.pushback(b, 5);
            char b2[5];
            buf.fetch(b2, 5);
            assert(!memcmp(b2, "Hello", 5)); });

    testcaseV("buffer", "send", [] () {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            ::buffer buf;
            buf.queue("ABC", 3);
            {   subscriber sub;
                auto res(buf.send(clientio::CLIENTIO,
                                  pipe.success().write,
                                  sub,
                                  Nothing));
                assert(res.issuccess());
                assert(res.success() == NULL); }
            assert(buf.empty());
            buf.queue("D", 1);
            {   subscriber sub;
                auto res(buf.send(clientio::CLIENTIO,
                                  pipe.success().write,
                                  sub,
                                  Nothing));
                assert(res.issuccess());
                assert(res.success() == NULL); }
            assert(buf.empty());
            char bb[4];
            auto res(pipe.success().read.read(clientio::CLIENTIO,
                                              bb,
                                              4,
                                              Nothing));
            assert(res.issuccess());
            assert(res.success() == 4);
            assert(!memcmp(bb, "ABCD", 4));
            pipe.success().close(); });

    testcaseV("buffer", "senderror", [] () {
            auto pipe(fd_t::pipe());
            pipe.success().close();
            ::buffer buf;
            buf.queue("HELLO", 5);
            subscriber sub;
            auto r(buf.send(clientio::CLIENTIO, pipe.success().write, sub));
            assert(r.isfailure());
            assert(r.failure() == error::from_errno(EBADF)); });

    testcaseV("buffer", "receive", [] () {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            {   auto res(pipe.success().write.write(clientio::CLIENTIO,
                                                    "GHI",
                                                    3));
                assert(res.issuccess());
                assert(res.success() == 3); }
            ::buffer buf;
            {   auto res(buf.receive(clientio::CLIENTIO, pipe.success().read));
                assert(res.isnothing());
                assert(buf.avail() == 3);
                char bb[2];
                buf.fetch(bb, 2);
                assert(!memcmp(bb, "GH", 2)); }
            {   auto res(pipe.success().write.write(clientio::CLIENTIO,
                                                    "JKLM",
                                                    4));
                assert(res.issuccess());
                assert(res.success() == 4); }
            {   auto res(buf.receive(clientio::CLIENTIO, pipe.success().read));
                assert(res.isnothing());
                assert(buf.avail() == 5);
                char bb[5];
                buf.fetch(bb, 5);
                assert(!memcmp(bb, "IJKLM", 5)); }
            pipe.success().close(); });

    testcaseV("buffer", "receivetimeout", [] () {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            ::buffer buf;
            {   auto res(buf.receive(
                             clientio::CLIENTIO,
                             pipe.success().read,
                             timestamp::now() + timedelta::milliseconds(10)));
                assert(res.isjust());
                assert(res.just() == error::timeout); }
            {   auto res(pipe.success().write.write(clientio::CLIENTIO,
                                                    "JKLM",
                                                    4));
                assert(res.issuccess());
                assert(res.success() == 4); }
            {   subscriber sub;
                auto res(buf.receive(clientio::CLIENTIO,
                                     pipe.success().read,
                                     sub));
                assert(res.issuccess());
                assert(res.success() == NULL);
                assert(buf.avail() == 4);
                char bb[4];
                buf.fetch(bb, 4);
                assert(!memcmp(bb, "JKLM", 4)); }
            pipe.success().close(); });

    testcaseV("buffer", "linearisestartofbuf", [] () {
            ::buffer buf;
            char b[16384];
            memset(b, 'Z', sizeof(b));
            for (int i = 0; i < 32; i++) buf.queue(b, sizeof(b));
            char *z = (char *)buf.linearise(0, sizeof(b) * 32);
            for (unsigned i = 0; i < sizeof(b) * 32; i++) {
                assert(z[i] == 'Z'); } });

    testcaseV("buffer", "linearisemultiplebuf", [] () {
            ::buffer buf;
            char b[16389];
            memset(b, 'Z', sizeof(b));
            for (int i = 0; i < 32; i++) buf.queue(b, sizeof(b));
            char *z = (char *)buf.linearise(sizeof(b), sizeof(b) * 31);
            for (unsigned i = 0; i < sizeof(b) * 30; i++) {
                assert(z[i] == 'Z'); } });

    testcaseV("buffer", "receivenotify", [] () {
            initpubsub();
            ::buffer buf;
            auto pipe(fd_t::pipe());
            subscriber sub;
            publisher pub;
            subscription scn(sub, pub);
            spark<bool> worker([&pub] () {
                    sleep(1);
                    pub.publish();
                    return true;});
            auto res(buf.receive(clientio::CLIENTIO,
                                 pipe.success().read,
                                 sub));
            assert(worker.ready());
            assert(res.issuccess());
            assert(res.success() == &scn);
            assert(worker.get() == true);
            pipe.success().close();
            deinitpubsub(clientio::CLIENTIO); });

    testcaseV("buffer", "receivefailure", [] () {
            ::buffer buf;
            auto pipe(fd_t::pipe());
            pipe.success().close();
            auto t(buf.receive(clientio::CLIENTIO, pipe.success().read));
            assert(t.isjust());
            assert(t.just() == error::from_errno(EBADF));
            assert(buf.empty()); });
}
