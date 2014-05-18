#include "buffer.H"

#include <string.h>

#include "fd.H"

#include "list.tmpl"

template class list<buffer::subbuf>;

buffer::subbuf *
buffer::newsubbuf(size_t sz, bool middle, bool atstart, bool insert)
{
    subbuf *res;
    size_t newsz;
    for (newsz = 16384; newsz < sz; newsz *= 2)
	;
    res = (subbuf *)malloc(newsz);
    res->sz = newsz - sizeof(*res);
    if (middle)
	res->prod = (newsz + sz) / 2;
    else
	res->prod = 0;
    res->cons = res->cons;
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
    if (last && last->sz - last->prod < 4096)
	b = last;
    else
	b = newsubbuf(0, false, false, true);
    auto read(fd.read(b->payload + b->prod, b->sz - b->prod));
    if (read.isfailure())
	return maybe<error>::mkjust(read.failure());
    b->prod += read.success();
    prod += read.success();
    return maybe<error>::mknothing();
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
	    return maybe<error>::mkjust(wrote.failure());
	first->cons += wrote.success();
	cons += wrote.success();
	return maybe<error>::mknothing();
    }
}

void
buffer::queue(const void *buf, size_t sz)
{
    auto tl(last);
    if (!tl || tl->sz - tl->prod < sz)
	tl = newsubbuf(sz, false, false, true);
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
    for (auto it(first); sz > 0; ) {
	/* Caller should check that there's enough stuff available before
	   calling. */
	assert(it);
	if (sz <= it->prod - it->cons) {
	    if (buf)
		memcpy(buf, it->payload + it->cons, sz);
	    it->cons += sz;
	    cons += sz;
	    return;
	} else {
	    memcpy(buf, it->payload + it->cons, it->prod - it->cons);
	    if (buf)
		buf = (void *)((unsigned long)buf + it->prod - it->cons);
	    sz += it->prod - it->cons;
	    auto n(it->next);
	    free(it);
	    it = n;
	}
    }
}

void
buffer::pushback(const void *what, size_t sz)
{
    auto b(first);
    if (!first || (first->cons < sz && first->cons != first->prod))
	b = newsubbuf(sz, true, true, true);
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
buffer::idx(size_t offset) const
{
    offset -= cons;
    auto it(first);
    while (1) {
	assert(it);
	if (offset < it->prod - it->cons)
	    return it->payload[it->cons + offset];
	offset -= it->prod - it->cons;
	it = it->next;
    }
}

void *
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
    auto b(newsubbuf(end - start, false, false, false));
    memcpy(b->payload,
	   it->payload + start,
	   it->prod - start);
    b->prod = it->prod - start;
    it->prod = start;
    auto n(it->next);
    b->next = n;
    it->next = b;
    it = n;
    while (b->prod < end) {
	if (it->prod - it->cons <= end - b->prod) {
	    memcpy(b->payload + b->prod,
		   it->payload + b->cons,
		   it->prod - it->cons);
	    b->prod += it->prod - it->cons;
	    it->cons = it->prod;
	} else {
	    memcpy(b->payload + b->prod,
		   it->payload + b->cons,
		   end - b->prod);
	    it->cons += end - b->prod;
	    b->prod += end;
	}
    }
    return b->payload;
}
