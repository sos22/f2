/* Randomly introduce sched_yield()s at various interesting points in
 * program execution in the hope of getting better scheduling
 * coverage. */
#include "fuzzsched.H"

#include <sched.h>
#include <stdlib.h>

bool
__do_fuzzsched;

void
_fuzzsched(void)
{
    if (random() % 10 == 0) sched_yield();
}
