#include <unistd.h>

#include "buffer.H"
#include "serialise.H"
#include "spark.H"
#include "test.H"
#include "timedelta.H"

#include "spark.tmpl"
#include "timedelta.tmpl"

void
tests::_buffer(void) {
    testcaseV("buffer", "basic", [] {
            ::buffer b;
            b.queue("HELLO", 5);
            assert(b.avail() == 5);
            assert(b.offset() == 0);
            assert(!memcmp(b.linearise(0, 5), "HELLO", 5));
            assert(!memcmp(b.linearise(3, 5), "LO", 2));
            assert(b.idx(2) == 'L');
            b.discard(3);
            assert(b.avail() == 2);
            assert(b.offset() == 3);
            assert(!memcmp(b.linearise(3, 5), "LO", 2));
            b.queue("WOO", 3);
            assert(b.avail() == 5);
            assert(b.offset() == 3);
            assert(!memcmp(b.linearise(3, 5), "LO", 2));
            assert(!memcmp(b.linearise(3, 8), "LOWOO", 5)); });
    testcaseS(
        "buffer",
        "fuzz",
        [] (support &t) {
            static const int nr_iters = 10;
            for (int i = 0; i < nr_iters; i++) {
                t.msg("iteration %d/%d", i, nr_iters);
                auto b = new ::buffer();

                unsigned char *content;
                size_t size;
                unsigned long prod;
                unsigned long cons;

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
                    switch (random() % 5) {
                    case 0: {
                        size_t sz = random() % 65536;
                        t.detail(" queue %zd", sz);
                        content = (unsigned char *)realloc(content, size + sz);
                        for (unsigned k = 0; k < sz; k++)
                            content[k + size] = (unsigned char)cntr++;
                        b->queue(content + size, sz);
                        size += sz;
                        prod += sz;
                        break;
                    }
                    case 1: {
                        if (!size)
                            continue;
                        size_t sz = (unsigned long)random() % size;
                        t.detail(" fetch %zd", sz);
                        unsigned char *buffer = (unsigned char *)malloc(sz);
                        b->fetch(buffer, sz);
                        cons += sz;
                        for (unsigned k = 0; k < sz; k++)
                            assert( buffer[k] == content[k] );
                        memmove(content, content + sz, size - sz);
                        size -= sz;
                        free(buffer);
                        break;
                    }
                    case 2: {
                        if (!size)
                            continue;
                        size_t sz = (unsigned long)random() % size;
                        t.detail(" discard %zd", sz);
                        b->discard(sz);
                        memmove(content, content + sz, size - sz);
                        cons += sz;
                        size -= sz;
                        break;
                    }
                    case 3: {
                        if (size <= 1)
                            continue;
                        size_t start = cons +
                            ((unsigned long)random() % (size - 1));
                        size_t end = start +
                            ((unsigned long)random() % (prod - start));
                        t.detail(" linearise %zd %zd", start, end);
                        const unsigned char *buf =
                            (const unsigned char *)b->linearise(start, end);
                        assert(buf);
                        for (unsigned k = 0; k < end - start; k++)
                            assert( buf[k] == content[k+start-cons]);
                        break;
                    }
                    case 4: {
                        if (!size)
                            continue;
                        size_t off = cons + ((unsigned long)random() % size);
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

    testcaseV("buffer", "serialise", [] {
            /* Normal case. */
            {   ::buffer inner;
                inner.queue("InNeR", 5);
                char ign;
                inner.fetch(&ign, 1);
                ::buffer outer;
                serialise1(outer).push(inner);
                deserialise1 ds(outer);
                ::buffer output(ds);
                assert(output.contenteq(inner));
                assert(!memcmp(output.linearise(1, 5), "nNeR", 4));
                /* Not sure whether it matters that the offset is
                 * preserved, but might as well test it. */
                assert(output.offset() == inner.offset()); }
            /* Empty buffers should work. */
            {   ::buffer inner;
                ::buffer outer;
                serialise1(outer).push(inner);
                deserialise1 ds(outer);
                ::buffer output(ds);
                assert(output.contenteq(inner)); }
            /* Implausibly enormous buffers should fail without
             * exploding. */
            {   ::buffer buf;
                serialise1 s(buf);
                s.push(1ul << 50);
                s.push(0ul);
                deserialise1 ds(buf);
                ::buffer output(ds);
                assert(ds.isfailure());
                assert(output.avail() == 0); }
            /* Similarly buffer lengths longer than the input. */
            {   ::buffer buf;
                serialise1 s(buf);
                s.push(2ul);
                s.push(0ul);
                s.push('X');
                deserialise1 ds(buf);
                ::buffer output(ds);
                assert(ds.isfailure());
                assert(output.avail() == 0); } });

    testcaseIO("buffer", "send", [] (clientio io) {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            ::buffer buf;
            buf.queue("ABC", 3);
            {   subscriber sub;
                auto res(buf.send(io,
                                  pipe.success().write,
                                  sub,
                                  Nothing));
                assert(res.issuccess());
                assert(res.success() == NULL); }
            assert(buf.empty());
            buf.queue("D", 1);
            {   subscriber sub;
                auto res(buf.send(io,
                                  pipe.success().write,
                                  sub,
                                  Nothing));
                assert(res.issuccess());
                assert(res.success() == NULL); }
            assert(buf.empty());
            char bb[4];
            auto res(pipe.success().read.read(io,
                                              bb,
                                              4,
                                              Nothing));
            assert(res.issuccess());
            assert(res.success() == 4);
            assert(!memcmp(bb, "ABCD", 4));
            pipe.success().close(); });

    testcaseIO("buffer", "senderror", [] (clientio io) {
            auto pipe(fd_t::pipe());
            pipe.success().close();
            ::buffer buf;
            buf.queue("HELLO", 5);
            subscriber sub;
            auto r(buf.send(io, pipe.success().write, sub));
            assert(r.isfailure());
            assert(r.failure() == error::from_errno(EBADF)); });

    testcaseIO("buffer", "receive", [] (clientio io) {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            {   auto res(pipe.success().write.write(io,
                                                    "GHI",
                                                    3));
                assert(res.issuccess());
                assert(res.success() == 3); }
            ::buffer buf;
            {   auto res(buf.receive(io, pipe.success().read));
                assert(res.issuccess());
                assert(buf.avail() == 3);
                char bb[2];
                buf.fetch(bb, 2);
                assert(!memcmp(bb, "GH", 2)); }
            {   auto res(pipe.success().write.write(io,
                                                    "JKLM",
                                                    4));
                assert(res.issuccess());
                assert(res.success() == 4); }
            {   auto res(buf.receive(io, pipe.success().read));
                assert(res.issuccess());
                assert(buf.avail() == 5);
                char bb[5];
                buf.fetch(bb, 5);
                assert(!memcmp(bb, "IJKLM", 5)); }
            pipe.success().close(); });

    testcaseIO("buffer", "fullreceive", [] (clientio io) {
            auto pipe(fd_t::pipe().fatal("pipe"));
            buffer b;
            for (unsigned x = 0; x < 1010; x++) {
                char somestuff[100];
                memset(somestuff, 'A', sizeof(somestuff));
                pipe.write.write(io, somestuff, sizeof(somestuff))
                    .fatal("write");
                b.receivefast(pipe.read).fatal("read"); }
            for (auto x(b.offset()); x < b.offset()+b.avail(); x++) {
                assert(b.idx(x) == 'A'); }
            pipe.close(); });

    testcaseIO("buffer", "rereceive", [] (clientio io) {
            /* Try to trigger the path where we have to memmove()
             * before receiving, rather than allocating another
             * buffer. */
            auto pipe(fd_t::pipe().fatal("pipe"));
            buffer b;
            char somestuff[16300];
            memset(somestuff, 'B', sizeof(somestuff));
            pipe.write.write(io, somestuff, sizeof(somestuff))
                .fatal("write");
            b.receivefast(pipe.read).fatal("read");
            b.discard(16200);
            pipe.write.write(io, somestuff, sizeof(somestuff))
                .fatal("write");
            b.receivefast(pipe.read).fatal("read2");
            for (auto x(b.offset()); x < b.offset()+b.avail(); x++) {
                assert(b.idx(x) == 'B'); }
            pipe.close(); });

    testcaseIO("buffer", "sendedges", [] (clientio) {
            auto pipe(fd_t::pipe().fatal("pipe"));
            buffer b;
            char somestuff[16300];
            memset(somestuff, 'C', sizeof(somestuff));
            b.queue(somestuff, sizeof(somestuff));
            b.queue(somestuff, sizeof(somestuff));
            /* Should now be two sub-buffers in the buffer.  Drain the
             * first one.*/
            b.sendfast(pipe.write).fatal("write1");
            assert(b.avail() == sizeof(somestuff));
            /* And the second one. */
            b.sendfast(pipe.write).fatal("write2");
            assert(b.avail() == 0);
            /* Try that again, but discard some bytes part-way through. */
            pipe.close();
            pipe = fd_t::pipe().fatal("pipe2");
            memset(somestuff, 'C', sizeof(somestuff));
            b.queue(somestuff, sizeof(somestuff));
            b.queue(somestuff, 10000);
            b.discard(sizeof(somestuff) - 10);
            /* Should now have a 10 byte sub-buffer followed by a 10k
             * one, which should get sent in a single write. */
            b.sendfast(pipe.write).fatal("write3");
            assert(b.avail() == 0);
            pipe.close(); });
    testcaseIO("buffer", "receivetimeout", [] (clientio io) {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            ::buffer buf;
            {   auto res(buf.receive(
                             io,
                             pipe.success().read,
                             timestamp::now() + timedelta::milliseconds(10)));
                assert(res.isfailure());
                assert(res.failure() == error::timeout); }
            {   auto res(pipe.success().write.write(io,
                                                    "JKLM",
                                                    4));
                assert(res.issuccess());
                assert(res.success() == 4); }
            {   subscriber sub;
                auto res(buf.receive(io,
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

    testcaseV("buffer", "linearisetype", [] {
            ::buffer buf;
            unsigned z = 12345;
            buf.queue("X", 1);
            buf.queue(&z, sizeof(z));
            unsigned *Z = buf.linearise<unsigned>(1);
            assert(*Z == z);
            *Z = 782;
            char chr;
            buf.fetch(&chr, 1);
            assert(chr == 'X');
            buf.fetch(&z, sizeof(z));
            assert(z == 782); });

    testcaseIO("buffer", "receivenotify", [] (clientio io) {
            ::buffer buf;
            auto pipe(fd_t::pipe());
            subscriber sub;
            publisher pub;
            subscription scn(sub, pub);
            spark<bool> worker([&pub] () {
                    sleep(1);
                    pub.publish();
                    return true;});
            auto res(buf.receive(io,
                                 pipe.success().read,
                                 sub));
            assert(res.issuccess());
            assert(res.success() == &scn);
            assert(worker.get() == true);
            pipe.success().close(); });

    testcaseIO("buffer", "receivefailure", [] (clientio io) {
            ::buffer buf;
            auto pipe(fd_t::pipe());
            pipe.success().close();
            auto t(buf.receive(io, pipe.success().read));
            assert(t.isfailure());
            assert(t == error::from_errno(EBADF));
            assert(buf.empty()); });

    testcaseV("buffer", "queueempty", [] () {
            ::buffer buf;
            buf.queue("", 0);
            assert(buf.avail() == 0);;
            buf.queue("X", 1);
            assert(buf.avail() == 1); });

    testcaseV("buffer", "clone", [] () {
            {   ::buffer buf1;
                ::buffer buf2(buf1);
                assert(buf2.avail() == 0); }
            for (int i = 0; i < 100; i++) {
                ::buffer buf1;
                for (int j = 0; j < 10; j++) {
                    switch (random() % 2) {
                    case 0: {
                        size_t sz(random() % 65536);
                        unsigned char b[sz];
                        memset(b, j, sz);
                        buf1.queue(b, sz); }
                    case 1: {
                        if (buf1.avail() == 0) continue;
                        size_t sz((unsigned long)random() % buf1.avail());
                        unsigned char b[sz];
                        buf1.fetch(b, sz); } } }
                ::buffer buf2(buf1);
                assert(buf1.offset() == buf2.offset());
                assert(buf1.avail() == buf2.avail());
                assert(buf1.contenteq(buf2));
                {   ::buffer buf3(buf1);
                    assert(buf1.offset() == buf3.offset());
                    assert(buf1.avail() == buf3.avail());
                    assert(buf1.contenteq(buf3)); }
                assert(buf1.offset() == buf2.offset());
                assert(buf1.avail() == buf2.avail());
                assert(buf1.contenteq(buf2)); } });

    testcaseV("buffer", "steal", [] () {
            ::buffer buf1;
            buf1.queue("HELLO", 5);
            char b;
            buf1.fetch(&b, 1);
            ::buffer buf2(Steal, buf1);
            assert(buf2.avail() == 4);
            assert(buf2.offset() == 1);
            assert(buf1.avail() == 0);
            assert(buf1.offset() == 1);
            assert(memcmp(buf2.linearise(1, 5), "ELLO", 4) == 0); });

    testcaseV("buffer", "constlinearise", [] () {
            ::buffer buf1;
            buf1.queue("HELLO", 5);
            const ::buffer &ref(buf1);
            assert(memcmp(ref.linearise(0, 5), "HELLO", 5) == 0); });

    testcaseV("buffer", "buffercmp", [] () {
            {   ::buffer buf1;
                ::buffer buf2;
                assert(buf1.contenteq(buf2));
                buf1.queue("HELLO", 5);
                assert(!buf1.contenteq(buf2));
                buf2.queue("H", 1);
                buf2.queue("E", 1);
                buf2.queue("L", 1);
                buf2.queue("L", 1);
                buf2.queue("O", 1);
                assert(buf1.contenteq(buf2));
                buf1.queue("A", 1);
                buf2.queue("B", 1);
                assert(!buf1.contenteq(buf2)); }
            {   ::buffer buf1;
                ::buffer buf2;
                buf1.queue("ABCD", 4);
                buf2.queue("CD", 2);
                assert(!buf1.contenteq(buf2));
                char b[2];
                buf1.fetch(b, 2);
                assert(buf1.contenteq(buf2)); } });

    testcaseV("buffer", "bufferfield", [] () {
            {   ::buffer buf;
                assert(strcmp(fields::mk(buf).c_str(), "<buffer: >") == 0); }
            {   ::buffer buf;
                assert(strcmp(fields::mk(buf).showshape().c_str(),
                              "<buffer: [!<0+0:3fe0-3fe0>]>") == 0); }
            {   ::buffer buf;
                buf.queue("AAAAAAAAAAAAAAAA", 16);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: A{16}>") == 0); }
            {   ::buffer buf;
                buf.queue("ABCDEFGHIJKLMNOP", 16);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: ABCDEFGHIJKLMNOP>") == 0); }
            {   ::buffer buf;
                buf.queue("AAABBBCCCDDDEEE", 15);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: AAABBBCCCDDDEEE>") == 0); }
            {   ::buffer buf;
                buf.queue("AAAABBBBCCCCDDDD", 16);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: A{4}B{4}C{4}D{4}>") == 0); }
            {   ::buffer buf;
                buf.queue("AAAABBBBBBCCCDDDDE", 18);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: A{4}B{6}CCCD{4}E>") == 0); }
            {   ::buffer buf;
                buf.queue("AAAABBBBCCCCDDDDE", 17);
                assert(strcmp(fields::mk(buf).showrepeats().c_str(),
                              "<buffer: AAAABBBBCCCCDDDDE>") == 0); }
            {   ::buffer buf;
                buf.queue("\\{]>\x5", 5);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: \\\\\\{\\]\\>\\x05>") == 0); }
            {   ::buffer buf;
                assert(strcmp(fields::mk(buf).bytes().c_str(),
                              "<buffer: >") == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x2\x3", 3);
                assert(strcmp(fields::mk(buf).bytes().c_str(),
                              "<buffer: 01.02.03>") == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x1\x1", 3);
                assert(strcmp(fields::mk(buf).bytes().c_str(),
                              "<buffer: 01{3}>") == 0); }
            {   ::buffer buf;
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: >") == 0); }
            {   ::buffer buf;
                buf.queue("\x1", 1);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 1/1>") == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x1\x1\x1\x1\x1\x1\x1", 8);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 101:0101:0101:0101>") == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x1\x1\x1\x1\x1\x1\x1\x1\x1\x1", 11);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 101:0101:0101:0101; 1:0101/3>")
                       == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x2\x3\x4\x5\x6\x7\x8\x9\xa\xb", 11);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 807:0605:0403:0201; B:0A09/3>")
                       == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x1\x1\x1\x1\x1\x1\x1", 8);
                buf.queue("\x1\x1\x1\x1\x1\x1\x1\x1", 8);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 101:0101:0101:0101{2}>") == 0); }
            {   ::buffer buf;
                buf.queue("HELLO", 5);
                char b;
                buf.fetch(&b, 1);
                assert(strcmp(
                           fields::mk(buf).showshape().c_str(),
                           "<buffer: [!<0+1:3fe0-3fdb ELLO>]>")
                       == 0); } });
    testcaseV("buffer", "transfer", [] {
            ::buffer buf1;
            buf1.queue("HELLO", 5);
            ::buffer buf2;
            buf2.transfer(buf1);
            assert(buf1.empty());
            assert(buf2.avail() == 5);
            char bytes[11];
            buf2.fetch(bytes, 3);
            assert(buf2.avail() == 2);
            assert(!memcmp(bytes, "HEL", 3));
            buf1.queue("GOODBYE", 7);
            buf2.transfer(buf1);
            assert(buf2.avail() == 9);
            buf2.fetch(bytes, 9);
            assert(!memcmp(bytes, "LOGOODBYE", 9)); });
    testcaseV("buffer", "fastio", [] {
            auto p(fd_t::pipe().fatal("pipe"));
            p.read.nonblock(true).fatal("nonblock read");
            p.write.nonblock(true).fatal("nonblock write");
            ::buffer buf1;
            assert(buf1.receivefast(p.read) == error::wouldblock);
            assert(buf1.empty());
            ::buffer buf2;
            buf2.queue("HELLO", 5);
            buf2.sendfast(p.write).fatal("sending fast");
            assert(buf2.empty());
            buf1.receivefast(p.read).fatal("receiving fast");
            assert(buf1.avail() == 5);
            assert(memcmp(buf1.linearise(0, 5), "HELLO", 5) == 0);
            size_t sent = 0;
            while (true) {
                auto a(buf2.avail());
                assert(a < 5);
                buf2.queue("HELLO", 5);
                auto r(timedelta::time<orerror<void> >([&buf2, &p] {
                            return buf2.sendfast(p.write); }));
                assert(r.td < timedelta::milliseconds(10));
                if (r.v == error::wouldblock) break;
                r.v.fatal("sending fast");
                assert(buf2.avail() < 5 + a);
                sent += 5 + a - buf2.avail(); }
            assert(sent >= 8192); });
}
