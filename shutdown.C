#include "shutdown.H"

#include <stdlib.h>

#include "waitbox.tmpl"

template class waitbox<shutdowncode>;

void
shutdowncode::finish()
{
    exit(code);
}

