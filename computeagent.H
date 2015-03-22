#ifndef COMPUTEAGENT_H__
#define COMPUTEAGENT_H__

class agentname;
class clientio;
class clustername;
class filename;
template <typename> class nnp;
template <typename> class orerror;

class computeagent {
public: static orerror<void> format(const filename &);
public: static orerror<nnp<computeagent> > build(clientio,
                                                 const clustername &,
                                                 const agentname &,
                                                 const agentname &,
                                                 const filename &); };

#endif /* !COMPUTEAGENT_H__ */
