#include "compute.H"

#include "either.H"
#include "fields.H"
#include "pair.H"
#include "serialise.H"

#include "either.tmpl"
#include "maybe.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"

const proto::compute::tag
proto::compute::tag::start(11);
const proto::compute::tag
proto::compute::tag::enumerate(12);
const proto::compute::tag
proto::compute::tag::drop(13);

proto::compute::tag::tag(deserialise1 &ds)
    : proto::tag(ds) {
    if (*this != start &&
        *this != enumerate &&
        *this != drop) {
        ds.fail(error::invalidmessage);
        *this = start; } }

const fields::field &
proto::compute::tag::field() const {
    if (*this == start) return fields::mk("start");
    else if (*this == enumerate) return fields::mk("enumerate");
    else if (*this == drop) return fields::mk("drop");
    else return fields::mk("unknown tag"); }

proto::compute::tasktag::tasktag(unsigned long _v) : v(_v) {}

proto::compute::tasktag::tasktag(deserialise1 &ds) : v(ds) {}

void
proto::compute::tasktag::serialise(serialise1 &s) const { s.push(v); }

const fields::field &
proto::compute::tasktag::field() const {
    return "T" + fields::padleft(
        fields::mk(v).sep(fields::period, 4).base(36).hidebase(),
        20,
        fields::mk("0000.")); }

proto::compute::tasktag
proto::compute::tasktag::invent() {
    return tasktag((unsigned long)random() ^
                   (unsigned long)random() << 16 ^
                   (unsigned long)random() << 32 ^
                   (unsigned long)random() << 48); }

proto::compute::jobstatus::jobstatus(const jobname &_name,
                                     tasktag _tag,
                                     const maybe<orerror<jobresult> > &_result)
    : name(_name),
      tag(_tag),
      result(_result) {}

proto::compute::jobstatus::jobstatus(deserialise1 &ds)
    : name(ds),
      tag(ds),
      result(ds) {}

void
proto::compute::jobstatus::serialise(serialise1 &s) const {
    s.push(name);
    s.push(tag);
    s.push(result); }

proto::compute::jobstatus
proto::compute::jobstatus::running(const jobname &jn, tasktag t) {
    return jobstatus(jn, t, Nothing); }

proto::compute::jobstatus
proto::compute::jobstatus::finished(const jobname &jn,
                                    tasktag t,
                                    const orerror<jobresult> &r) {
    return jobstatus(jn, t, r); }

const fields::field &
proto::compute::jobstatus::field() const {
    const fields::field *acc =
        &("<jobstatus: name:" +
          fields::mk(name) +
          " tag:" +
          fields::mk(tag));
    if (result == Nothing) return *acc + " running>";
    else if (result.just().isfailure()) {
        return *acc + " failed:" + fields::mk(result.just().failure()) + ">"; }
    else {
        return *acc + " " + result.just().success().field() + ">"; } }

proto::compute::event::event(const contentT &c) : content(c) {}

void
proto::compute::event::serialise(serialise1 &s) const { s.push(content); }

proto::compute::event::event(deserialise1 &ds) : content(ds) {}

proto::compute::event
proto::compute::event::start(const jobname &jn, const tasktag &tag) {
    return contentT(Left(), Left(), mkpair(jn, tag)); }
maybe<pair<jobname, proto::compute::tasktag> >
proto::compute::event::start() const {
    if (content.isright() || content.left().isright()) return Nothing;
    else return content.left().left(); }

proto::compute::event
proto::compute::event::finish(const jobstatus &js) {
    return contentT(Left(), Right(), js); }
maybe<pair<jobname, pair<orerror<jobresult>, proto::compute::tasktag> > >
proto::compute::event::finish() const {
    if (content.isright() || content.left().isleft()) return Nothing;
    auto &js(content.left().right());
    return mkpair(js.name, mkpair(js.result.just(), js.tag)); }

proto::compute::event
proto::compute::event::removed(const jobstatus &js) {
    return contentT(Right(), js); }

const fields::field &
proto::compute::event::field() const {
    if (content.isright()) {
        return "<removed:" + fields::mk(content.right()) + ">"; }
    else if (content.left().isright()) {
        return "<finish:" + fields::mk(content.left().right()) + ">"; }
    else {
        return "<start:" + fields::mk(content.left().left().first()) +
            "::" + fields::mk(content.left().left().second()) +
            ">"; } }
