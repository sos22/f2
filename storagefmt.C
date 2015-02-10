#include "err.h"

#include "fields.H"
#include "filename.H"
#include "storageslave.H"

int
main(int argc, char *argv[]) {
    if (argc != 2) {
        errx(1, "need a single argument: the filename for the storage area"); }
    filename basename(argv[1]);
    storageslave::format(basename)
        .fatal("formatting storage pool " + fields::mk(basename));
    return 0; }