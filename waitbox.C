#include "waitbox.H"
#include "waitbox.tmpl"

void
waitbox<void>::set() {
    assert(!ready());
    auto token(mux.lock());
    content = maybe<void>::just;
    mux.unlock(&token);
    pub_.publish(); }

void
waitbox<void>::get(clientio io) const {
    subscriber sub;
    subscription s(sub, pub_);
    while (!ready()) sub.wait(io); }

maybe<void>
waitbox<void>::get(clientio io, timestamp deadline) const {
    subscriber sub;
    subscription s(sub, pub_);
    while (!ready()) {
        auto r = sub.wait(io, deadline);
        if (r == NULL) return Nothing; }
    return maybe<void>::just; }

bool
waitbox<void>::ready() const {
    auto token(mux.lock());
    auto res(content.isjust());
    mux.unlock(&token);
    return res; }

maybe<void>
waitbox<void>::poll() const {
    if (ready()) return maybe<void>::just;
    else return Nothing; }
