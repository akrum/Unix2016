#include "server.h"

#include "handler.h"
#include "resources.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BACKLOG 10   // how many pending connections queue will hold
#define SHOULD_USE_TCP_CORK 0
#define DEBUG_MODE 1

#if(DEBUG_MODE == 1)
#define DEBUG_PRINT(...) {do{printf(__VA_ARGS__);}while(0);}
#define DEBUG_PRINT_IF(condition, ...) {do{if((condition)){printf(__VA_ARGS__);};}while(0);}
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINT_IF(condition, ...)
#endif

static int CreateSocketToListen(uint16_t port) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    char portStr[32];
    sprintf(portStr, "%hu", port);

    int rv;
    if ((rv = getaddrinfo(NULL, portStr, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        int yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            close(sockfd);
            continue;
        }

#if (SHOULD_USE_TCP_CORK)
        yes = 1;
        if (setsockopt(sockfd, IPPROTO_TCP, TCP_CORK, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            close(sockfd);
            continue;
        }
#endif

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("server: bind");
            close(sockfd);
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return -1;
    }
    return sockfd;
}

static bool RunServerImpl(int sockfd) {
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        return false;
    }

    while (1) {  // main accept() loop
        struct sockaddr_storage theirAddr;
        socklen_t addrSize = sizeof theirAddr;
        int newfd = accept(sockfd, (struct sockaddr*)&theirAddr, &addrSize);
        if (newfd == -1) {
            perror("accept");
            continue;
        }

        DEBUG_PRINT("received new connection so creating new process\n");

        const pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            continue;
        }
        if (pid == 0) { // this is the child process
            #ifdef DEBUG
            fprintf(stderr, "Child born\n");
            #endif
            close(sockfd); // child doesn't need the listener
            ServeClient(newfd);
            #ifdef DEBUG
            fprintf(stderr, "Child dead\n");
            #endif
            exit(0);
        }
        close(newfd); // parent doesn't need this
    }
}

static bool IgnoreSignal(int sigNum) {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN; // handle signal by ignoring
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(sigNum, &sa, NULL) == -1) {
        perror("sigaction");
        return false;
    }
    return true;
}

bool RunServer(uint16_t port) {
    if(!preload_pictures())
    {
        return false;
    }
    
    if (!IgnoreSignal(SIGCHLD) || !IgnoreSignal(SIGPIPE))
    {
        return false;
    }
    int sockfd = CreateSocketToListen(port);
    if (sockfd == -1)
    {
        return false;
    }
    printf("server: waiting for connections on http://localhost:%hu/\n", port);
    bool res = RunServerImpl(sockfd);
    close(sockfd);
    return res;
}
