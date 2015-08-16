#ifndef JOBAPIIMPL_H__
#define JOBAPIIMPL_H__

class jobapi;
template <typename> class waitbox;

jobapi &newjobapi(waitbox<void> &);
void deletejobapi(jobapi &);

#endif /* !JOBAPIIMPL_H__ */
