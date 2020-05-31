#include "http_request.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECV_BUF_SIZE 4096
#define CONNECTION_KEEP_ALIVE "Connection: keep-alive"

/**
 * THttpRequest
 */

void THttpRequest_Init(struct THttpRequest* self) {
    memset(self, 0, sizeof(struct THttpRequest));
    self->should_keep_alive = false;
}

void THttpRequest_Destroy(struct THttpRequest* self) {
    free(self->Method);
    free(self->Path);
    free(self->QueryString);
}

static void SplitFullRequest(char* fullRequest, char** path, char** queryString) {
    char* pos = strchr(fullRequest, '?');
    if (pos == NULL) {
        // no query string
        *path = strdup(fullRequest);
        *queryString = NULL;
    } else {
        *pos = '\0';
        *path = strdup(fullRequest);
        *queryString = strdup(pos + 1);
    }
}

static bool ParseRequestLine(char* line, struct THttpRequest* out) {
    char* saveptr;

    char* method = strtok_r(line, " ", &saveptr);
    if (method == NULL) {
        return false;
    }
    out->Method = strdup(method);

    char* fullRequest = strtok_r(NULL, " ", &saveptr);
    if (fullRequest == NULL) {
        return false;
    }

    SplitFullRequest(fullRequest, &out->Path, &out->QueryString);
    return true;
}

static bool ParseHeaderLine(char* line, struct THttpRequest* out) {
    // TODO
    if(strcmp(line, CONNECTION_KEEP_ALIVE) == 0)
    {
        out->should_keep_alive = true;
    }
    return true;
}

/**
 * THttpRequestParser
 */

struct THttpRequestParser {
    struct TStringBuilder Line;
    size_t LineNum;
    bool Complete;
    bool Invalid;
};

void THttpRequestParser_Init(struct THttpRequestParser* self) {
    TStringBuilder_Init(&self->Line);
    self->LineNum = 0;
    self->Complete = false;
    self->Invalid = false;
}

void THttpRequestParser_Destroy(struct THttpRequestParser* self) {
    TStringBuilder_Destroy(&self->Line);
}

static void ProcessLine(struct THttpRequestParser* parser, struct THttpRequest* request) {
    // HTTP uses CR+LF (\r\n) to separate the lines
    TStringBuilder_ChopSuffix(&parser->Line, "\r\n");
    // fprintf(stderr, "Line [len=%zu]: %s\n", parser->Line.Length, parser->Line.Data);

    if (parser->Line.Length == 0) {
        // We do not know how to parse request body, if any:(
        parser->Complete = true;
    } 
    else 
    {
        if (parser->LineNum == 0) 
        {
            if (!ParseRequestLine(parser->Line.Data, request)) 
            {
                parser->Invalid = true;
            }
        }
        else
        {
            if (!ParseHeaderLine(parser->Line.Data, request)) {
                parser->Invalid = true;
            }
        }
    }

    TStringBuilder_Clear(&parser->Line);
}

static size_t Consume(struct THttpRequestParser* parser, const char* data, size_t size, struct THttpRequest* request) {
    size_t total = 0;
    while (size != 0) {
        const char* eolnPtr = memchr(data, '\n', size);
        size_t partLen;
        if (eolnPtr != NULL) {
            partLen = eolnPtr - data + 1; // 1 for newline
            TStringBuilder_AppendBuf(&parser->Line, data, partLen);
            ProcessLine(parser, request);
            parser->LineNum++;
        } else {
            partLen = size;
            TStringBuilder_AppendBuf(&parser->Line, data, partLen);
        }
        data += partLen;
        size -= partLen;
        total += partLen;
        if (parser->Complete) {
            break;
        }
    }
    return total;
}

bool THttpRequest_Receive(struct THttpRequest* self, int sockfd) {
    char buf[RECV_BUF_SIZE];
    bool result = true;

    struct THttpRequestParser parser;
    THttpRequestParser_Init(&parser);

    while (1) {
        ssize_t ret = recv(sockfd, buf, RECV_BUF_SIZE, 0);
        if (ret == -1) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            perror("recv");
            result = false;
            break;
        }
        if (ret == 0) {
            // other peer has disconnected
            result = false;
            break;
        }

        const size_t consumed = Consume(&parser, buf, ret, self);
        if (consumed != (size_t)ret) {
            parser.Invalid = true;
        }
        if (parser.Complete) {
            break;
        }
    }
    if (parser.Invalid) {
        result = false;
    }

    THttpRequestParser_Destroy(&parser);
    return result;
}
