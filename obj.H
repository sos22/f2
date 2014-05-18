#ifndef OBJ_H__
#define OBJ_H__

#include <sys/types.h>

/* Base class for all classes.  Only effect is to make sure that
   everything has a placement new operator */
class obj {
public:
    void *operator new(size_t /*sz*/, void *buf)
	{
	    return buf;
	}
    void *operator new(size_t sz)
	{
	    return ::operator new(sz);
	}
};

void *operator new(size_t sz, void *buf);

#endif
