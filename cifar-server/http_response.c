#include "http_response.h"

#include "io.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <assert.h>
#include "resources.h"

#include <stdio.h>
#include <string.h>

#define CRLF "\r\n"
#define CONNECTION_KEEP_ALIVE "Connection: keep-alive"

const char* GetReasonPhrase(enum EHttpCode code) {
    switch (code) {
        case HTTP_OK:
            return "OK";
        case HTTP_BAD_REQUEST:
            return "Bad Request";
        case HTTP_NOT_FOUND:
            return "Not Found";
        case HTTP_METHOD_NOT_ALLOWED:
            return "Method Not Allowed";
        case HTTP_INTERNAL_SERVER_ERROR:
            return "Internal Server Error";
        default:
            return "";
    }
}

void THttpResponse_Init(struct THttpResponse* self) {
    self->Code = HTTP_OK;
    self->ContentType = NULL;
    self->should_use_sendfile = false;
    self->file_path_requested = NULL;
    TStringBuilder_Init(&self->Body);
}

bool THttpResponse_Send(struct THttpResponse* self, int sockfd) {
    const size_t contentLength = self->Body.Length;

    struct TStringBuilder headers;
    TStringBuilder_Init(&headers);

    TStringBuilder_Sprintf(&headers, "HTTP/1.1 %d %s" CRLF, self->Code, GetReasonPhrase(self->Code));
    TStringBuilder_Sprintf(&headers, CONNECTION_KEEP_ALIVE CRLF);
    TStringBuilder_Sprintf(&headers, CUSTOM_LINE_FOR_WARMUP CRLF);

    if (self->ContentType) {
        TStringBuilder_Sprintf(&headers, "Content-Type: %s" CRLF, self->ContentType);
    }
    TStringBuilder_Sprintf(&headers, "Content-Length: %zu" CRLF, contentLength);
    TStringBuilder_AppendCStr(&headers, CRLF);

    // fprintf(stderr, "RESPONSE {%s}\n", headers.Data);

    bool result = true;

    if (!SendAll(sockfd, headers.Data, headers.Length)) {
        result = false;
    }

    if (result && self->Body.Length != 0) {
        if (!SendAll(sockfd, self->Body.Data, self->Body.Length)) {
            result = false;
        }
    }

    if(result && self->should_use_sendfile)
    {
        assert(self->file_path_requested != NULL);
        int sent_file_fd = open(self->file_path_requested, O_RDONLY);
        
        if (sent_file_fd == -1) 
        {
            perror("open file:");
            printf("fd is -1\n");
            CreateErrorPage(self, HTTP_NOT_FOUND);
            return false;
        }

        result &= send_with_sendfile(sockfd, sent_file_fd);
        close(sent_file_fd);
    }

    TStringBuilder_Destroy(&headers);
    return result;
}

void THttpResponse_Destroy(struct THttpResponse* self) {
    TStringBuilder_Destroy(&self->Body);
}
