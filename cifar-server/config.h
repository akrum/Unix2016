#ifndef CONFIG_INCLUDE_GUARD
#define CONFIG_INCLUDE_GUARD

#define TRUE 1
#define FALSE 0

// server config
#define SERVER_DEBUG_MODE TRUE

#define BACKLOG 10   // how many pending connections queue will hold
#define SHOULD_USE_TCP_CORK FALSE
#define SHOULD_USE_THREADS TRUE
#define USING_THREAD_POOL TRUE
#define NUM_THREADS 5


// hadler config
#define HANDLER_DEBUG_MODE FALSE


// http_request config
#define RECV_BUF_SIZE 4096
#define TIMEOUT_FOR_KEEP_ALIVE_CONNECTIONS 10 * 1000  // 10 seconds


// http_response config
#define HTTP_RESPONSE_DEBUG_MODE FALSE

#define TIME_BUFFER_SIZE 1000


// io.c config
#define IO_C_DEBUG_MODE FALSE

#define MAX_RESEND_ATTEMPTS 5


// resources config
#define RESOURCES_DEBUG_MODE FALSE

#define USING_SENDFILE TRUE
#define USING_MMAP_INSTEAD_READ TRUE




#endif