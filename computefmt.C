/* Formatting a compute slave is pretty easy, since it has no state
 * beyond an event queue. */
#include "err.h"

#include "computeagent.H"
#include "fields.H"
#include "filename.H"
#include "main.H"

#include "orerror.tmpl"

orerror<void>
f2main(list<string> &args) {
    if (args.length() != 1) {
        errx(1,
             "need a single argument: the filename for "
             "the compute state file"); }
    filename basename(args.idx(0));
    computeagent::format(basename)
        .fatal("formatting compute state file " + fields::mk(basename));
    return Success; }
