#ifndef TEST2_H__
#define TEST2_H__

#include "filename.H"
#include "list.H"
#include "map.H"
#include "percentage.H"
#include "string.H"

class clientio;
template <typename> class maybe;

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
private: string name;
private: list<filename> files;
private: list<filename> dependencies;
private: LineCoverage linecoverage;
private: BranchCoverage branchcoverage;
private: map<string, std::function<void ()> > tests;
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
public:  void runtest(const string &name) const;
public:  void runtests() const;
public:  void prepare() const; };

#endif /* !TEST2_H__ */
