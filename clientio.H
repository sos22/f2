/* Dummy type indicating that a function can perform synchronous
 * client IO.  Passing a clientio argument into a function gives it
 * permission to block for a potentially long time waiting for IO with
 * a remote host.  You generally don't want to see clientio and mutex
 * tokens in the same function. */
#ifndef CLIENTIO_H__
#define CLIENTIO_H__

class clientio {
private: clientio();
public:  static clientio CLIENTIO;
};

#endif /* CLIENTIO_H__ */
