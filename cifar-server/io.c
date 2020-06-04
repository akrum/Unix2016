#include "io.h"
#include "config.h"

#include <sys/socket.h>
#include <sys/types.h>

#if !defined(__APPLE__)
#include <sys/sendfile.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#define DEBUG_MODE IO_C_DEBUG_MODE

#if(DEBUG_MODE == 1)
#define DEBUG_PRINT(...) {do{printf(__VA_ARGS__);}while(0);}
#define DEBUG_PRINT_IF(condition, ...) {do{if((condition)){printf(__VA_ARGS__);};}while(0);}
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINT_IF(condition, ...)
#endif

#define UNUSED_VAR(var) ((void) var)


bool SendAll(int sockfd, const void* data, size_t len)
{
    while (len != 0) {
        ssize_t ret = send(sockfd, data, len, 0);
        if (ret == -1) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            perror("send");
            return false;
        }
        data += ret;
        len -= ret;
    }
    return true;
}

bool send_with_sendfile(int sock_fd, int file_fd, int file_size)
{
    UNUSED_VAR(file_size);
    #if defined(__APPLE__) || defined(__OSX__)
    int attempt_counter = 0;
    while(attempt_counter < MAX_RESEND_ATTEMPTS)
    {
        off_t offset = 0;
        off_t bytes_sent = 0;
        ssize_t ret = sendfile(file_fd, sock_fd, offset, &bytes_sent, NULL, 0);
        if(ret == -1)
        {
            if (errno == EINTR || errno == EAGAIN) 
            {
                DEBUG_PRINT("sendfile error, trying again\n");
                attempt_counter++;
                continue;
            }
            perror("sendfile error:");
            return false;
        }
        return true;
    }
    #else
    // TODO: may be unstable
    int attempt_counter = 0;
    while(attempt_counter < MAX_RESEND_ATTEMPTS)
    {
        off_t offset = 0;
        off_t bytes_sent = 0;
        ssize_t ret = sendfile(sock_fd, file_fd, 0, file_size);
        if(ret == -1)
        {
            if (errno == EINTR || errno == EAGAIN) 
            {
                DEBUG_PRINT("sendfile error, trying again\n");
                attempt_counter++;
                continue;
            }
            perror("sendfile error:");
            return false;
        }
        return true;
    }
    #endif

    return false;
}