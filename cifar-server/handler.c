#include "handler.h"

#include "http_request.h"
#include "http_response.h"
#include "resources.h"
#include "stringutils.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#ifdef DEBUG
static const char* SafeStr(const char* value) {
    return (value != NULL) ? value : "<null>";
}
#endif

#define DEBUG_MODE 0

#if(DEBUG_MODE == 1)
#define DEBUG_PRINT(...) {do{printf(__VA_ARGS__);}while(0);}
#define DEBUG_PRINT_IF(condition, ...) {do{if((condition)){printf(__VA_ARGS__);};}while(0);}
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINT_IF(condition, ...)
#endif

static void Handle(const struct THttpRequest* request, struct THttpResponse* response) {
    #ifdef DEBUG
    fprintf(
        stderr, "method: '%s'; path: '%s'; qs: '%s'\n",
        SafeStr(request->Method), SafeStr(request->Path), SafeStr(request->QueryString)
    );
    #endif

    if (strcasecmp(request->Method, "GET") != 0) { // case-insensitive compare
        CreateErrorPage(response, HTTP_METHOD_NOT_ALLOWED);
        return;
    }

    if (strcmp(request->Path, "/") == 0) {
        int page = request->QueryString ? GetIntParam(request->QueryString, "page") : 0;
        CreateIndexPage(response, page);
        return;
    }
    if (StartsWith(request->Path, "/images/")) {
        int n;
        if (sscanf(request->Path, "/images/%d.bmp", &n) == 1) {
            SendCifarBitmap(response, n);
            return;
        }
    }
    if (StartsWith(request->Path, "/static/")) {
        SendStaticFile(response, request->Path + 1);
        return;
    }

    CreateErrorPage(response, HTTP_NOT_FOUND);
}

void ServeClient(int sockfd) {
    bool should_keep_alive = true;

    while(should_keep_alive)
    {
        struct THttpRequest req;
        struct THttpResponse resp;

        THttpRequest_Init(&req);
        THttpResponse_Init(&resp);

        http_receive_result_t receive_result = THttpRequest_Receive(&req, sockfd, should_keep_alive);
        if (RECEIVE_RESULT_SUCCESS == receive_result) 
        {
            DEBUG_PRINT("received good request, now handling it\n");

            Handle(&req, &resp);
            THttpResponse_Send(&resp, sockfd);

            DEBUG_PRINT_IF(req.should_keep_alive, 
                           "received keep alive connection flag so not closing the socket\n");
            should_keep_alive &= req.should_keep_alive;
        } 
        else if(RECEIVE_RESULT_BAD_REQUEST == receive_result)
        {
            CreateErrorPage(&resp, HTTP_BAD_REQUEST);
            THttpResponse_Send(&resp, sockfd);
            should_keep_alive = false;
        }
        else if(RECEIVE_RESULT_ERROR == receive_result)
        {
            CreateErrorPage(&resp, HTTP_INTERNAL_SERVER_ERROR);
            THttpResponse_Send(&resp, sockfd);
            should_keep_alive = false;
        }
        else if(RECEIVE_RESULT_DISCONNECTED == receive_result)
        {
            DEBUG_PRINT("the connection was interrupted so closing the socket\n");

            close(sockfd);
            return;
        }
        else
        {
            assert(false); // unreachable
        }

        THttpResponse_Destroy(&resp);
        THttpRequest_Destroy(&req);
    }

}
