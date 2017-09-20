#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUF_MAX_LEN    8192
#define DEFAULT_PORT   56789
#define DEFAULT_IP     "139.199.220.252"

int sock_init(void)
{
    struct sockaddr_in server_addr;
    int sock = -1;
    int ret = -1;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        perror("socket error: ");
        return sock;
    }
    fprintf(stdout, "Create socket success\n");

    memset(&server_addr, 0x00, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_PORT);

    ret = inet_aton(DEFAULT_IP, (struct in_addr *) &server_addr.sin_addr.s_addr);
    if(0 == ret){
        perror("inet aton error");
        goto out;
    }
    ret = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if(0 != ret){
        perror("connect error");
        goto out;
    }
    fprintf(stdout, "connect to server %s success\n", DEFAULT_IP);

out:
    close(sock);
    return sock;
}

int sock_uninit(int *sock)
{
    if(*sock > 0){
        close(*sock);
        *sock = -1;
    }
}

int sock_send(int sock, const char *buf, int len)
{
    int ret = -1;

    if(sock < 0 || NULL == buf || len <= 0){
        fprintf(stdout, "%s param invalid\n", __FUNCTION__);
        return -1;
    }

    ret = send(sock, buf, len, 0);
    if(ret < 0) 
        perror("send failed");
#if DEBUG
    else 
        fprintf(stdout, "Send msg successfully[%d]\n", ret);
#endif

    return ret;
}

int sock_recv(int sock, char *buf, int len)
{
    fd_set rd_fds;
    struct timeval timeout;
    int max_fd = -1;
    int ret = -1;

    if(sock < 0 || NULL == buf || len <= 0){
        fprintf(stdout, "%s param invalid\n", __FUNCTION__);
        return -1;
    }

    FD_ZERO(&rd_fds);
    FD_SET(sock, &rd_fds);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    max_fd = sock + 1;
    ret = select(max_fd, &rd_fds, NULL, NULL, &timeout);
    if(-1 == ret){
        perror("select failed: ");
        return -1;
    }else if(0 == ret){
        fprintf(stdout, "Timeout, and try again\n");
        return 0;
    }       
    
    memset(buf, 0x00, len);    
    ret = recv(sock, buf, len, 0); 
    if(ret > 0){ 
        printf("Get a msg: %s\n", buf);
    }else if(0 == ret){
        fprintf(stdout, "Maybe server has been disconnected\n");
        return -2;      // disconnect
    }else{
        if(EINTR == errno){
            fprintf(stdout, "Get a signal\n");
        }   
        perror("Recv msg failed");
        return -1;
    }

    return ret;
}

