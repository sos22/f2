#ifndef ENUMERATE_H__
#define ENUMERATE_H__

#include "list.H"

template <typename t> list<t> enumerate(void);

class enumerator {
    template <typename t> friend list<t> enumerate(void);
    /* Avoid silliness */
private: enumerator(const enumerator &) = delete;
private: void operator=(const enumerator &) = delete;
    /* Only constructed by enumerate() */
private: enumerator() {}
private: ~enumerator() {}
private: typedef list<unsigned> path;
    /* Paths which we still need to explore, for iterative deepening
     * BFS */
private: list<path> pendingpaths;
    /* What still needs to be done on the current path to get back to
     * the frontier. */
private: path schedule;
    /* Path from the root to the current node. */
private: path currentpath;
public:  template <typename t> t range(t, t); };

template <> unsigned enumerator::range(unsigned, unsigned);

#endif /* !ENUMERATE_H__ */
