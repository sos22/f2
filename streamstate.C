#include "streamstate.H"

#include "either.H"
#include "fields.H"
#include "parsers.H"

#include "either.tmpl"
#include "parsers.tmpl"

__parsermkoperpipepipe(streamstate)

const fields::field &
fields::mk(const streamstate &o) {
    if (o.isempty()) return mk("[empty]");
    else if (o.ispartial()) return mk("[partial]");
    else {
        assert(o.iscomplete());
        return mk("[complete]"); } }

const streamstate
streamstate::empty(0);
const streamstate
streamstate::partial(1);
const streamstate
streamstate::complete(2);

streamstate::streamstate(int _fl)
    : fl(_fl) {}

bool
streamstate::isempty() const { return fl == 0; }
bool
streamstate::ispartial() const { return fl == 1; }
bool
streamstate::iscomplete() const { return fl == 2; }

const parser<streamstate> &
parsers::streamstate() {
    return strmatcher("[empty]", streamstate::empty) ||
        strmatcher("[partial]", streamstate::partial) ||
        strmatcher("[complete]", streamstate::complete); }
