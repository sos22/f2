#include "err.h"

#include "fields.H"
#include "filename.H"
#include "main.H"
#include "storageagent.H"

#include "orerror.tmpl"

orerror<void>
f2main(list<string> &args) {
    if (args.length() != 1) {
        errx(1, "need a single argument: the filename for the storage area"); }
    filename basename(args.idx(0));
    storageagent::format(basename)
        .fatal("formatting storage pool " + fields::mk(basename));
    return Success; }
