#ifndef VOID_H__
#define VOID_H__

class deserialise1;
class serialise1;

/* This can be used as a template parameter in a lot of places where
 * void would be a syntax error. */
class Void {
public: Void() {}
public: bool operator==(Void) const { return true; }
public: void serialise(serialise1&) const {}
public: Void(deserialise1 &) {} };

#endif /* VOID_H__ */
