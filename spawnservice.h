#ifndef SPAWNSERVICE_H__
#define SPAWNSERVICE_H__

/* FDs for communicating with the parent. */
/* FD for us to send responses to our parent process. */
#define RESPFD 3
/* FD on which the parent will send us requests */
#define REQFD 4

#ifdef __cplusplus
namespace spawn {
#endif

struct message {
    enum {
        msgexecfailed = 9,
        msgexecgood,
        msgsendsignal,
        msgsentsignal,
        msgchilddied,
    } tag;
    union {
        struct {
            int err;
        } execfailed;
        struct {
        } execgood;
        struct {
            int signr;
        } sendsignal;
        struct {
            int err;
        } sentsignal;
        struct {
            int status;
        } childdied;
    };
};

#ifdef __cplusplus
}
#endif

#endif /* !SPAWNSERVICE_H__ */
