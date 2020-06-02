#include "http_response.h"

#include "io.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include "resources.h"

#include <stdio.h>
#include <string.h>

#define CRLF "\r\n"
#define CONNECTION_KEEP_ALIVE "Connection: keep-alive"
#define DEBUG_MODE 1
#define TIME_BUFFER_SIZE 1000

#if(DEBUG_MODE == 1)
#define DEBUG_PRINT(...) {do{printf(__VA_ARGS__);}while(0);}
#define DEBUG_PRINT_IF(condition, ...) {do{if((condition)){printf(__VA_ARGS__);};}while(0);}
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINT_IF(condition, ...)
#endif


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
    self->sent_file_size = 0;
    self->file_modification_time = 0;
    TStringBuilder_Init(&self->Body);
}

bool THttpResponse_Send(struct THttpResponse* self, int sockfd) {
    size_t contentLength = self->Body.Length;
    if(self->should_use_sendfile)
    {
        contentLength = self->sent_file_size;
    }

    struct TStringBuilder headers;
    TStringBuilder_Init(&headers);

    TStringBuilder_Sprintf(&headers, "HTTP/1.1 %d %s" CRLF, self->Code, GetReasonPhrase(self->Code));
    TStringBuilder_Sprintf(&headers, CONNECTION_KEEP_ALIVE CRLF);
    TStringBuilder_Sprintf(&headers, CUSTOM_LINE_FOR_WARMUP CRLF);

    if(self->should_use_sendfile)
    {
        DEBUG_PRINT("adding mtime header from %li\n", self->file_modification_time);
        char time_string_buf[TIME_BUFFER_SIZE];
        memset(time_string_buf, 0, sizeof(char) * TIME_BUFFER_SIZE);
        struct tm tm = *gmtime(&self->file_modification_time);
        strftime(time_string_buf, sizeof(time_string_buf) , "%a, %d %b %Y %H:%M:%S %Z", &tm);
        
        DEBUG_PRINT("will add time header: %s\n", time_string_buf);
        TStringBuilder_Sprintf(&headers, "Date: %s" CRLF, time_string_buf);
    }

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
        DEBUG_PRINT("sending file %s\n", self->file_path_requested);
        
        if (sent_file_fd == -1) 
        {
            perror("open file:");
            printf("fd is -1\n");
            CreateErrorPage(self, HTTP_NOT_FOUND);
            return false;
        }

        result &= send_with_sendfile(sockfd, sent_file_fd, self->sent_file_size);
        close(sent_file_fd);
    }

    TStringBuilder_Destroy(&headers);
    return result;
}

void THttpResponse_Destroy(struct THttpResponse* self) {
    TStringBuilder_Destroy(&self->Body);
}
