#include "waitbox.H"
#include "waitbox.tmpl"

void
waitbox<void>::set() { waitbox<Void>::set(Void()); }

template void waitbox<Void>::set(const Void &);
template const Void &waitbox<Void>::get(clientio) const;
template maybe<Void> waitbox<Void>::get(clientio, timestamp) const;
template bool waitbox<Void>::ready() const;
