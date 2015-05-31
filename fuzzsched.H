#ifndef FUZZSCHED_H__
#define FUZZSCHED_H__

extern bool __do_fuzzsched;

void _fuzzsched(void);

static inline void
fuzzsched() { if (__do_fuzzsched) _fuzzsched(); }

#endif /* !FUZZSCHED_H__ */
