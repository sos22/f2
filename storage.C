#include "storage.H"

#include "error.H"
#include "serialise.H"

#include "either.tmpl"
#include "fields.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "serialise.tmpl"

const proto::storage::tag
proto::storage::tag::createjob(90);
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
proto::storage::tag::removejob(100);

proto::storage::tag::tag(deserialise1 &ds)
    : proto::tag(ds) {
    if (*this != createjob &&
        *this != append &&
        *this != finish &&
        *this != read &&
        *this != listjobs &&
        *this != statjob &&
        *this != liststreams &&
        *this != statstream &&
        *this != removejob) {
        ds.fail(error::invalidmessage);
        *this = createjob; } }

const fields::field &
proto::storage::tag::field() const {
#define X(n) if (*this == n) return fields::mk(#n)
    X(createjob);
    X(append);
    X(finish);
    X(read);
    X(listjobs);
    X(statjob);
    X(liststreams);
    X(statstream);
    X(removejob);
#undef X
    return fields::mk("<bad tag>"); }

proto::storage::listjobsres::listjobsres(proto::eq::eventid _when,
                                         const list<jobname> &_res)
    : when(_when),
      res(_res) {}

proto::storage::listjobsres::listjobsres(deserialise1 &ds)
    : when(ds),
      res(ds) {}

void
proto::storage::listjobsres::serialise(serialise1 &s) const {
    s.push(when);
    s.push(res); }

const fields::field &
fields::mk(const proto::storage::listjobsres &a) {
    return "<listjobsres: when:" + fields::mk(a.when) +
        " res:" + fields::mk(a.res) +
        ">"; }

proto::storage::liststreamsres::liststreamsres(proto::eq::eventid _when,
                                               const list<streamstatus> &_res)
    : when(_when),
      res(_res) {}

proto::storage::liststreamsres::liststreamsres(deserialise1 &ds)
    : when(ds),
      res(ds) {}

void
proto::storage::liststreamsres::serialise(serialise1 &s) const {
    s.push(when);
    s.push(res); }

const fields::field &
fields::mk(const proto::storage::liststreamsres &a) {
    return "<liststreamsres: when:" + fields::mk(a.when) +
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
proto::storage::event::finishstream(const jobname &j,
                                    const streamname &s,
                                    const streamstatus &stat) {
    return event(t_finishstream, j, s, stat); }

void
proto::storage::event::serialise(serialise1 &s) const {
    s.push((int)typ);
    s.push(job);
    switch (typ) {
    case t_newjob:
    case t_removejob:
        break;
    case t_finishstream:
        s.push(stream.just());
        s.push(status.just());
        break; } }

proto::storage::event::event(deserialise1 &ds)
    : typ((type)ds.poprange<int>(t_newjob, t_finishstream)),
      job(ds),
      stream(Nothing),
      status(Nothing) {
    switch (typ) {
    case t_newjob:
    case t_removejob:
        return;
    case t_finishstream:
        stream.mkjust(ds);
        status.mkjust(ds);
        return; }
    abort(); }


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
    case t_finishstream:
        base = &fields::mk("finish ");
        break; }
    assert(base != NULL);
    return *base + fields::mk(job) +
        "::" + fields::mk(stream) +
        "->" + fields::mk(status); }

bool
proto::storage::event::operator==(const event &o) const {
    return typ == o.typ &&
        job == o.job &&
        stream == o.stream &&
        status == o.status; }
