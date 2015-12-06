#ifndef TEST2_H__
#define TEST2_H__

#include "filename.H"
#include "list.H"
#include "map.H"
#include "pair.H"
#include "percentage.H"
#include "string.H"

/* RUNNING_ON_VALGRIND is a statement-expression, so can't be used in
 * template arguments. Turn it into a bool. */
bool running_on_valgrind();

#define TIMEDILATE (running_on_valgrind() ? 20 : 1)

class clientio;
template <typename> class maybe;

class testmodule;

class testresultaccumulator {
private: list<pair<pair<string, string> , timedelta> > results;
public:  void result(const testmodule &, const string &, timedelta);
public:  void dump(const maybe<filename> &) const; };

class testmodule {
public:  class LineCoverage {
    public: percentage p;
    public: explicit LineCoverage(percentage _p) : p(_p) {} };
public:  class BranchCoverage {
    public: percentage p;
    public: explicit BranchCoverage(percentage _p) : p(_p) {} };
public:  class Dependency {
    public: filename fn;
    public: explicit Dependency(const filename &_fn) : fn(_fn) {} };
public:  class TestFlags {
    private: TestFlags() = delete;
    private: unsigned flags;
    private: explicit TestFlags(unsigned _flags);
    public:  static TestFlags dflt(); };
public:  class TestCase {
    private: TestCase() = delete;
    private: TestCase(const TestCase &) = delete;
    private: void operator=(const TestCase &) = delete;
    public:  TestCase(TestFlags,const string &, const std::function<void ()> &);
    public:  TestFlags flags;
    public:  string name;
    public:  std::function<void ()> work; };
private: string _name;
public:  const string &name() const { return _name; }
private: list<filename> _files;
public:  const list<filename> &files() const { return _files; }
private: list<filename> dependencies;
private: LineCoverage linecoverage;
private: BranchCoverage branchcoverage;
private: map<string, TestCase> tests;
private: void applyinit();
private: template <typename ... argsT> void applyinit(
    LineCoverage,
    argsT &&...args);
private: template <typename ... argsT> void applyinit(
    BranchCoverage,
    argsT &&...args);
private: template <typename ... argsT> void applyinit(
    const Dependency &,
    argsT &&...args);
private: template <typename ... argsT> void applyinit(
    TestFlags, const string &s, const std::function<void ()> &f,
    argsT &&...args);
private: template <typename ... argsT> void applyinit(
    TestFlags, const string &s, const std::function<void (clientio)> &f,
    argsT &&...args);
private: template <typename ... argsT> void applyinit(
    const string &s, const std::function<void ()> &f,
    argsT &&...args);
private: template <typename ... argsT> void applyinit(
    const string &s, const std::function<void (clientio)> &f,
    argsT &&...args);
public:  template <typename ... argsT> testmodule(
    const string &_name,
    const list<filename> &_files,
    argsT &&...args);
public:  void listtests() const;
public:  void printmodule() const;
public:  void runtest(const string &name,
                      maybe<timedelta>,
                      testresultaccumulator &) const;
public:  void runtests(maybe<timedelta>, testresultaccumulator &) const;
public:  void prepare() const; };

#endif /* !TEST2_H__ */
