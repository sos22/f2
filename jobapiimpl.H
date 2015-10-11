#ifndef JOBAPIIMPL_H__
#define JOBAPIIMPL_H__

class jobapi;
class job;
class storageclient;

jobapi &newjobapi(storageclient &sc, const job &);
void deletejobapi(jobapi &);

#endif /* !JOBAPIIMPL_H__ */
