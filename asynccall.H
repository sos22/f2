/* Bits to make it easier to write wrappers on async pool calls. */
#ifndef ASYNCCALL_H__
#define ASYNCCALL_H__

class clientio;
namespace fields { class field; }
template <typename> class maybe;
template <typename> class orerror;
class publisher;

/* A wrapper around connpool::asynccall which specialises it for a
 * particular operation. _descr is a struct type whose only members
 * are typedefs saying how to do the specialisation:
 *
 * _resT: The ultimate result of the call. This is automatically
 * wrapped in orerror<>.
 *
 * _friend: A thing to which this asyncall is a friend i.e. the type
 * which can call impl().  You usually want _friend to be a friend to
 * the asynccall type, as well, to make the pop() implementation
 * easier.
 *
 * _implT: Type type which actually implements the call. These have
 * two mandatory fields: api, which should be an instance of the
 * asynccall type, and cl, which should be a reference to the connpool
 * asynccall.
 *
 * _innerTokenT: the connpool token type used for tracking when the inner
 * call finishes.
 *
 * The asynccall type should only be created with new (and definitely
 * not on the stack), because otherwise pop() and abort() won't work.
 * asynccall.tmpl provides reasonable default definitions of all of
 * the member functions except for pop(token), which must be provided
 * by whoever provides implT.
 */
template <typename _descr> class asynccall {
    friend typename _descr::_friend;
private: typedef typename _descr::_resT resT;
private: typedef typename _descr::_implT implT;
private: typedef typename _descr::_innerTokenT innerTokenT;
private: asynccall() {}
private: asynccall(const asynccall &) = delete;
private: void operator=(const asynccall &) = delete;
private: ~asynccall() {}
private: implT &impl();
private: const implT &impl() const;
public:  class token {
        friend asynccall;
    private: innerTokenT inner;
    private: token(innerTokenT _inner) : inner(_inner) {}
    public:  const fields::field &field() const; };
public:  maybe<token> finished() const;
public:  const publisher &pub() const;
public:  orerror<resT> pop(token t);
public:  orerror<resT> pop(clientio io);
public:  void abort(); };

#endif /* !ASYNCCALL_H__ */
