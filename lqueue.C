#include "lqueue.H"

#include "fields.H"
#include "list.H"
#include "logging.H"

#include "fields.tmpl"
#include "list.tmpl"

genevtsrc::genevtsrc(size_t _sz, timedelta _maxwait)
    : head(NULL),
      tail(NULL),
      mux(),
      dests(),
      destsnonempty(false),
      sequence(100),
      pub(),
      sz(_sz),
      maxwait(_maxwait) {}

bool
genevtsrc::push(slot &slt) {
    slt.next = NULL;
    auto now(timestamp::now());
    slt.pushedat = now;
    auto token(mux.lock());
    if (dests.empty()) {
        mux.unlock(&token);
        return false; }
    slt.idx = sequence++;
    if (head == NULL) {
        assert(tail == NULL);
        head = &slt; }
    else {
        tail->next = &slt;
        if (now - head->pushedat > maxwait) {
            logmsg(loglevel::failure,
                   "lqueue getting backlogged; waiting " +
                   fields::mk(now - head->pushedat));
            /* Don't warn for a bit. */
            for (auto cursor(head); cursor != NULL; cursor = cursor->next) {
                cursor->pushedat = now + 1_s; } } }
    tail = &slt;
    mux.unlock(&token);
    pub.publish();
    return true; }

genevtsrc::~genevtsrc() { assert(dests.empty()); }

genevtdest::genevtdest(genevtsrc &_src)
    : sequence(0),
      src(_src) {
    src.mux.locked([this] {
            sequence = src.sequence;
            src.destsnonempty.store(true);
            src.dests.pushtail(this); }); }

genevtsrc::slot *
genevtdest::startpop() {
    auto tok(src.mux.lock());
    auto it(src.head);
    while (it != NULL) {
        if (it->idx == sequence) break;
        assert(it->idx < sequence);
        it = it->next; }
    src.mux.unlock(&tok);
    return it; }

bool
genevtdest::finishpop(genevtsrc::slot &slt) {
    assert(slt.idx == sequence);
    auto tok(src.mux.lock());
    sequence++;
    bool advance = &slt == src.head;
    for (auto it(src.dests.start()); advance && !it.finished(); it.next()) {
        advance = (*it)->sequence != slt.idx; }
    if (advance) {
        src.head = src.head->next;
        if (src.head == NULL) src.tail = NULL; }
    src.mux.unlock(&tok);
    return advance; }

genevtsrc::slot *
genevtdest::unsubscribe() {
    auto tok(src.mux.lock());
    src.dests.drop(this);
    if (src.dests.empty()) src.destsnonempty.store(false);
    /* Try to advance the queue */
    auto firsttokeep(src.head);
    genevtsrc::slot *lasttodrop(NULL);
    while (firsttokeep != NULL) {
        bool keep = false;
        for (auto it(src.dests.start());
             !keep && !it.finished();
             it.next()) {
            assert((*it)->sequence >= firsttokeep->idx);
            keep = (*it)->sequence == firsttokeep->idx; }
        if (keep) break;
        lasttodrop = firsttokeep;
        firsttokeep = lasttodrop->next; }
    auto res(src.head);
    if (lasttodrop == NULL) res = NULL;
    else lasttodrop->next = NULL;
    src.head = firsttokeep;
    if (src.head == NULL) src.tail = NULL;
    src.mux.unlock(&tok);
    return res; }
