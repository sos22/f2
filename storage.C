#include "storage.H"

#include "error.H"
#include "serialise.H"

#include "serialise.tmpl"

#include "fieldfinal.H"

const proto::storage::tag
proto::storage::tag::createjob(90);
const proto::storage::tag
proto::storage::tag::createstream(91);
const proto::storage::tag
proto::storage::tag::append(92);
const proto::storage::tag
proto::storage::tag::finish(93);
const proto::storage::tag
proto::storage::tag::read(94);
const proto::storage::tag
proto::storage::tag::listjobs(95);
const proto::storage::tag
proto::storage::tag::liststreams(96);
const proto::storage::tag
proto::storage::tag::statstream(97);
const proto::storage::tag
proto::storage::tag::removestream(98);
const proto::storage::tag
proto::storage::tag::removejob(99);

proto::storage::tag::tag(unsigned x) : v(x) {}

proto::storage::tag::tag(deserialise1 &ds)
    : v(ds) {
    if (*this != createjob &&
        *this != createstream &&
        *this != append &&
        *this != finish &&
        *this != read &&
        *this != listjobs &&
        *this != liststreams &&
        *this != statstream &&
        *this != removestream &&
        *this != removejob) {
        ds.fail(error::invalidmessage);
        v = createjob.v; } }

void
proto::storage::tag::serialise(serialise1 &s) const { s.push(v); }

proto::storage::event::event(
    type t,
    const jobname &j,
    const maybe<streamname> &s)
    : typ(t),
      job(j),
      stream(s) {}

proto::storage::event
proto::storage::event::newjob(const jobname &j) {
    return event(t_newjob, j, Nothing); }

proto::storage::event
proto::storage::event::removejob(const jobname &j) {
    return event(t_removejob, j, Nothing); }

proto::storage::event
proto::storage::event::newstream(const jobname &j, const streamname &s) {
    return event(t_newstream, j, s); }

proto::storage::event
proto::storage::event::finishstream(const jobname &j, const streamname &s) {
    return event(t_finishstream, j, s); }

proto::storage::event
proto::storage::event::removestream(const jobname &j, const streamname &s) {
    return event(t_removestream, j, s); }

void
proto::storage::event::serialise(serialise1 &s) const {
    s.push((int)typ);
    s.push(job);
    s.push(stream); }

proto::storage::event::event(deserialise1 &ds)
    : typ((type)ds.poprange<int>(t_newjob, t_removestream)),
      job(ds),
      stream(ds) {}

const fields::field &
proto::storage::event::field() const {
    const fields::field *base;
    base = NULL;
    switch (typ) {
    case t_newjob:
        base = &fields::mk("newjob ");
        break;
    case t_removejob:
        base = &fields::mk("removejob ");
        break;
    case t_newstream:
        base = &fields::mk("newstream ");
        break;
    case t_finishstream:
        base = &fields::mk("finish ");
        break;
    case t_removestream:
        base = &fields::mk("removestream ");
        break;
    }
    assert(base != NULL);
    return *base + fields::mk(job) + "::" + fields::mk(stream); }
