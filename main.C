#include "main.H"

#include "list.H"
#include "string.H"

#include "list.tmpl"
#include "orerror.tmpl"

int
main(int argc, char *argv[]) {
    list<string> args;
    for (int i = 1; i < argc; i++) args.pushtail(argv[i]);
    f2main(args).fatal("error from f2main");
    return 0; }

/* Basically anything which defines f2main() will need this, so put it
 * here rather than makign everyone include list.tmpl. */
template string &list<string>::idx(unsigned);
template unsigned list<string>::length() const;
