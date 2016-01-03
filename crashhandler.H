#ifndef CRASHHANDLER_H__
#define CRASHHANDLER_H__

namespace fields { class field; }

/* Callbacks which get invoked when we know we're going to crash, to
 * collect any interesting logging. There can be any manner of weird
 * memory corruption when these get invoked, and there might certainly
 * be any set of locks held. The crash handlers are all invoked in
 * newly-fork()ed processes, so you're effectively
 * single-threaded. There's a one second timeout on each one; the
 * expectation is that the vast majority will complete more quickly
 * than that. */
class crashhandler {
private:   crashhandler *next;
private:   crashhandler *prev;
private:   char *name;
protected: explicit crashhandler(const fields::field &_name);
protected: ~crashhandler();
private:   crashhandler(const crashhandler&) = delete;
private:   void operator=(const crashhandler &) = delete;
private:   virtual void doit() = 0;
public:    static void invoke();
public:    static bool crashing(); };

#endif /* !CRASHHANDLER_H__ */
