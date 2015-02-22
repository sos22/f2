/* Formatting a compute slave is pretty easy, since it has no state
 * beyond an event queue. */
#include "err.h"

#include "computeagent.H"
#include "fields.H"
#include "filename.H"

int
main(int argc, char *argv[]) {
    if (argc != 2) {
        errx(1,
             "need a single argument: the filename for "
             "the compute state file"); }
    filename basename(argv[1]);
    computeagent::format(basename)
        .fatal("formatting compute state file " + fields::mk(basename));
    return 0; }
