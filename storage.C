#include "storage.H"

#include "error.H"
#include "serialise.H"

#include "list.tmpl"
#include "maybe.tmpl"
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
proto::storage::tag::statjob(96);
const proto::storage::tag
proto::storage::tag::liststreams(97);
const proto::storage::tag
proto::storage::tag::statstream(98);
const proto::storage::tag
proto::storage::tag::removestream(99);
const proto::storage::tag
proto::storage::tag::removejob(100);

proto::storage::tag::tag(unsigned x) : v(x) {}

proto::storage::tag::tag(deserialise1 &ds)
    : v(ds) {
    if (*this != createjob &&
        *this != createstream &&
        *this != append &&
        *this != finish &&
        *this != read &&
        *this != listjobs &&
        *this != statjob &&
        *this != liststreams &&
        *this != statstream &&
        *this != removestream &&
        *this != removejob) {
        ds.fail(error::invalidmessage);
        v = createjob.v; } }

void
proto::storage::tag::serialise(serialise1 &s) const { s.push(v); }

proto::storage::listjobsres::listjobsres(proto::eq::eventid _when,
                                         const maybe<jobname> &_start,
                                         const maybe<jobname> &_end,
                                         const list<jobname> &_res)
    : when(_when),
      start(_start),
      end(_end),
      res(_res) {}

proto::storage::listjobsres::listjobsres(deserialise1 &ds)
    : when(ds),
      start(ds),
      end(ds),
      res(ds) {}

void
proto::storage::listjobsres::serialise(serialise1 &s) const {
    s.push(when);
    s.push(start);
    s.push(end);
    s.push(res); }

const fields::field &
fields::mk(const proto::storage::listjobsres &a) {
    return "<listjobsres: when:" + fields::mk(a.when) +
        " start:" + fields::mk(a.start) +
        " end:" + fields::mk(a.end) +
        " res:" + fields::mk(a.res) +
        ">"; }

proto::storage::liststreamsres::liststreamsres(proto::eq::eventid _when,
                                               const maybe<streamname> &_start,
                                               const maybe<streamname> &_end,
                                               const list<streamstatus> &_res)
    : when(_when),
      start(_start),
      end(_end),
      res(_res) {}

proto::storage::liststreamsres::liststreamsres(deserialise1 &ds)
    : when(ds),
      start(ds),
      end(ds),
      res(ds) {}

void
proto::storage::liststreamsres::serialise(serialise1 &s) const {
    s.push(when);
    s.push(start);
    s.push(end);
    s.push(res); }

const fields::field &
fields::mk(const proto::storage::liststreamsres &a) {
    return "<liststreamsres: when:" + fields::mk(a.when) +
        " start:" + fields::mk(a.start) +
        " end:" + fields::mk(a.end) +
        " res:" + fields::mk(a.res) +
        ">"; }

proto::storage::event::event(
    type t,
    const jobname &j,
    const maybe<streamname> &s,
    const maybe<streamstatus> &stat)
    : typ(t),
      job(j),
      stream(s),
      status(stat) {}

proto::storage::event
proto::storage::event::newjob(const jobname &j) {
    return event(t_newjob, j, Nothing, Nothing); }

proto::storage::event
proto::storage::event::removejob(const jobname &j) {
    return event(t_removejob, j, Nothing, Nothing); }

proto::storage::event
proto::storage::event::newstream(const jobname &j, const streamname &s) {
    return event(t_newstream, j, s, Nothing); }

proto::storage::event
proto::storage::event::finishstream(const jobname &j,
                                    const streamname &s,
                                    const streamstatus &stat) {
    return event(t_finishstream, j, s, stat); }

proto::storage::event
proto::storage::event::removestream(const jobname &j, const streamname &s) {
    return event(t_removestream, j, s, Nothing); }

void
proto::storage::event::serialise(serialise1 &s) const {
    s.push((int)typ);
    s.push(job);
    switch (typ) {
    case t_newjob:
    case t_removejob:
        break;
    case t_newstream:
    case t_removestream:
        s.push(stream.just());
        break;
    case t_finishstream:
        s.push(stream.just());
        s.push(status.just());
        break; } }

proto::storage::event::event(deserialise1 &ds)
    : typ((type)ds.poprange<int>(t_newjob, t_removestream)),
      job(ds),
      stream(Nothing),
      status(Nothing) {
    switch (typ) {
    case t_newjob:
    case t_removejob:
        break;
    case t_newstream:
    case t_removestream:
        stream.mkjust(ds);
        break;
    case t_finishstream:
        stream.mkjust(ds);
        status.mkjust(ds);
        break; } }


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
    return *base + fields::mk(job) +
        "::" + fields::mk(stream) +
        "->" + fields::mk(status); }
