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
    subscription s(sub, pub);
    while (!ready()) sub.wait(io); }

bool
waitbox<void>::ready() const {
    auto token(mux.lock());
    auto res(content.isjust());
    mux.unlock(&token);
    return res; }

template class waitbox<bool>;
