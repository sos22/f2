#include "void.H"

#include "fields.H"

const fields::field &
Void::field() const { return fields::mk(""); }
