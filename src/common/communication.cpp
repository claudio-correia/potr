#include "communication.h"

#include <arpa/inet.h>
#include <iostream>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>

int Communication::wait_connection(int portno)
{
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        perror("Failed to create listen socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    char yes = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(listenfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Failed to bind to listening address");
        return 1;
    }

    listen(listenfd, 1); // only queue one connection

    this->sockfd = accept(listenfd, 0, 0);
    if (this->sockfd < 0)
    {
        perror("Failed to connect to client");
        return 1;
    }

    setsockopt(this->sockfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    std::cout << "Connected to client" << std::endl;
    return 0;
}

int Communication::connect_to(const char *IPaddr, int portno)
{
    this->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->sockfd < 0)
    {
        perror("Failed to create connection socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(IPaddr);
    serv_addr.sin_port = htons(portno);

    if (connect(this->sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)))
    {
        perror("Failed to connect to server");
        return 1;
    }

    char yes = 1;
    setsockopt(this->sockfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    std::cout << "Connected to server" << std::endl;
    return 0;
}

void Communication::close_connection()
{
    close(this->sockfd);
    std::cout << "Closed connection" << std::endl;
}

int Communication::read_int(int *msg)
{
    if (read(this->sockfd, msg, sizeof(int)) < 0)
    {
        perror("Failed to read integer");
        return 1;
    }

    return 0;
}

int Communication::read_buf(void *msg, int size)
{
    for (int bytes_read = 0; bytes_read < size;)
    {
        int b = read(this->sockfd, ((unsigned char *)msg + bytes_read), (size - bytes_read));
        if (b < 0)
        {
            perror("Failed to read buffer");
            return 1;
        }
        bytes_read += b;
    }

    return 0;
}

int Communication::send_int(int msg)
{
    if (write(this->sockfd, &msg, sizeof(msg)) < 0)
    {
        perror("Failed to send integer");
        return 1;
    }

    return 0;
}

int Communication::send_buf(const void *msg, int size)
{
    if (write(this->sockfd, msg, size) < 0)
    {
        perror("Failed to send buffer");
        return 1;
    }

    return 0;
}
