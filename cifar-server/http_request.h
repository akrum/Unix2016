#pragma once

#include "stringbuilder.h"

#include <stdbool.h>

struct THttpRequest {
    char* Method;
    char* Path;
    char* QueryString;
    bool should_keep_alive;
};

typedef enum http_receive_result
{
    RECEIVE_RESULT_SUCCESS,
    RECEIVE_RESULT_DISCONNECTED,
    RECEIVE_RESULT_ERROR,
    RECEIVE_RESULT_BAD_REQUEST,
}http_receive_result_t;

void THttpRequest_Init(struct THttpRequest* self);
http_receive_result_t THttpRequest_Receive(struct THttpRequest* self, int sockfd);
void THttpRequest_Destroy(struct THttpRequest* self);
