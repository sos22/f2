#include "orerror.H"

#include "fields.H"

const _Success
Success;

const fields::field &
_Success::field() const { return fields::mk("Success"); }
