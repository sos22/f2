#include "buffer.H"

#include <string.h>

#include "fd.H"
#include "test.H"

#include "list.tmpl"

template class list<buffer::subbuf>;

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
buffer::receive(fd_t fd)
{
    subbuf *b;
    if (last && last->sz - last->prod >= 4096)
        b = last;
    else
        b = extend_end();
    auto read(fd.read(b->payload + b->prod, b->sz - b->prod));
    if (read.isfailure())
        return read.failure();
    b->prod += read.success();
    prod += read.success();
    return Nothing;
}

maybe<error>
buffer::send(fd_t fd)
{
    while (1) {
        /* Shouldn't try to send an empty buffer */
        assert(first);
        if (first->prod == first->cons) {
            auto b = first;
            first = b->next;
            free(b);
            continue;
        }
        auto wrote(fd.write(first->payload + first->cons,
                            first->prod - first->cons));
        if (wrote.isfailure())
            return wrote.failure();
        first->cons += wrote.success();
        cons += wrote.success();
        return Nothing;
    }
}

void
buffer::queue(const void *buf, size_t sz)
{
    auto tl(last);
    if (!tl || tl->sz - tl->prod < sz)
        tl = extend_end(sz);
    memcpy(tl->payload + tl->prod, buf, sz);
    tl->prod += sz;
    prod += sz;
}

bool
buffer::empty() const
{
    for (auto i(first); i; i = i->next) {
        if (i->cons < i->prod)
            return false;
    }
    return true;
}

buffer::~buffer(void)
{
    auto i(first);
    auto n(first);
    while (i) {
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
    for (auto it(first); it; it = it->next)
        ack += it->prod - it->cons;
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
        if (sz <= first->prod - first->cons) {
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
    }
}

void
buffer::pushback(const void *what, size_t sz)
{
    assert(cons >= sz);
    auto b(first);
    if (b && b->cons == b->prod && b->sz >= sz && b->cons < sz) {
        b->prod = (sz + b->sz) / 2;
        b->cons = b->prod;
        assert(b->cons <= b->sz);
    } else if (!(b && b->cons >= sz)) {
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
        if (off < it->prod - it->cons)
            return it->payload[it->cons + off];
        off -= it->prod - it->cons;
        it = it->next;
    }
}

const void *
buffer::linearise(size_t start, size_t end)
{
    assert(start >= cons);
    assert(end <= prod);
    assert(start <= end);
    start -= cons;
    end -= cons;
    auto it(first);
    while (1) {
        assert(it);
        if (start >= it->prod - it->cons) {
            start -= it->prod - it->cons;
            end -= it->prod - it->cons;
            it = it->next;
            continue;
        }
        if (end <= it->prod - it->cons)
            return it->payload + start + it->cons;
        break;
    }
    /* Going to have to actually linearise. */
    auto b(unlinked_subbuf(end - start));
    memcpy(b->payload,
           it->payload + it->cons + start,
           it->prod - it->cons - start);
    b->prod = it->prod - it->cons - start;
    assert(b->cons == 0);
    assert(b->sz >= end - start);
    it->prod = it->cons + start;
    auto n(it->next);
    b->next = n;
    it->next = b;
    it = n;
    end -= start;
    while (b->prod < end) {
        assert(it);
        if (it->prod - it->cons < end - b->prod) {
            memcpy(b->payload + b->prod,
                   it->payload + it->cons,
                   it->prod - it->cons);
            b->prod += it->prod - it->cons;
            it->cons = it->prod;
            /* Could unlink @it from the list here, but it's easier to
               leave it around, and it might be useful later if we
               continue manipulating the list. */
            assert(it->next);
            it = it->next;
        } else {
            memcpy(b->payload + b->prod,
                   it->payload + it->cons,
                   end - b->prod);
            it->cons += end - b->prod;
            b->prod = end;
        }
    }
    return b->payload;
}

void
buffer::test(class test &t)
{
    for (int i = 0; i < 1000; i++) {
        t.msg("iteration %d/%d", i, 1000);
        auto b = new buffer();

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
                    assert( ((char *)buf)[k] == content[k + start - cons]);
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
}
