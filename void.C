#include "void.H"

#include "fields.H"
#include "parsers.H"

#include "parsers.tmpl"

const fields::field &
Void::field() const { return fields::mk(""); }

const ::parser<Void> &
Void::parser() { return nulparser<Void>(Void()); }
