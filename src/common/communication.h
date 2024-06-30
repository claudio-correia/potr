#pragma once

class Communication
{
private:
    int sockfd;

public:
    int wait_connection(int port);
    int connect_to(const char *ip, int port);
    void close_connection();
    int read_int(int *msg);
    int read_buf(void *msg, int size);
    int send_int(int msg);
    int send_buf(const void *msg, int size);
};