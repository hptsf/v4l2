#ifndef __SOCK_H__
#define __SOCK_H__

int sock_init(void);
int sock_uninit(int *sock);
int sock_send(int sock, const char *buf, int len);
int sock_recv(int sock, char *buf, int len);

#endif
