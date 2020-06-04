#include "server.h"
#include "config.h"

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
#include <pthread.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DEBUG_MODE SERVER_DEBUG_MODE

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
#if !(defined(__APPLE__) || defined(__OSX__))
        yes = 1;
        if (setsockopt(sockfd, IPPROTO_TCP, TCP_CORK, &yes, sizeof(int)) == -1)
        {
            perror("setsockopt");
            close(sockfd);
            continue;
        }
#else  // __APPLE__ or __OSX__ is defined
        yes = 1;
        if(setsockopt(sockfd, IPPROTO_TCP, TCP_NOPUSH, &yes, sizeof(int)) == -1)
        {
            perror("setsockopt tcp_nopush");
            close(sockfd);
            continue;
        }
#endif // __APPLE__
#endif // SHOULD_USE_TCP_CORK

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

#if (SHOULD_USE_THREADS)
#if !(USING_THREAD_POOL)
void* server_thread_main(void* serve_fd_ptr)
{
    int serve_fd = *((int *) serve_fd_ptr);
    DEBUG_PRINT("thread will work with fd: %d\n", serve_fd);

    ServeClient(serve_fd);
    close(serve_fd);
    
    return NULL;
}

static bool RunServerImpl(int sockfd)
{
    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        return false;
    }

    int thread_array_index = 0;
    int create_thread_result = -1;
    int thread_await_result = -1;

    pthread_t *created_threads[NUM_THREADS];
    int created_thread_fd_args[NUM_THREADS];
    memset(created_threads, 0, sizeof(pthread_t *) * NUM_THREADS); // initially all threads are zero pointers

    while (1)
    {  // main accept() loop
        struct sockaddr_storage theirAddr;
        socklen_t addrSize = sizeof theirAddr;
        int newfd = accept(sockfd, (struct sockaddr*)&theirAddr, &addrSize);
        if (-1 == newfd) {
            perror("accept");
            continue;
        }

        DEBUG_PRINT("received new connection so creating new process\n");

        bool thread_creation_succeeded = false;
        while(!thread_creation_succeeded)
        {
            if(NULL == created_threads[thread_array_index])
            {
                created_threads[thread_array_index] = malloc(sizeof(pthread_t));
                memset(created_threads[thread_array_index], 0, sizeof(pthread_t));
                created_thread_fd_args[thread_array_index] = newfd;

                DEBUG_PRINT("passing fd %d to thread\n", created_thread_fd_args[thread_array_index]);
                create_thread_result = pthread_create(created_threads[thread_array_index], NULL, server_thread_main, (void*) &created_thread_fd_args[thread_array_index]);
                if (NULL != create_thread_result)
                {
                    free(created_threads[thread_array_index]);
                    created_threads[thread_array_index] = NULL;
                    created_thread_fd_args[thread_array_index] = -1;
                }
                thread_creation_succeeded = true;
                thread_array_index = (thread_array_index + 1) % NUM_THREADS;
            }
            else
            {
                thread_await_result = pthread_join(created_threads[thread_array_index], NULL);
                DEBUG_PRINT_IF(NULL != thread_await_result, "thread join error!, skipping\n");

                free(created_threads[thread_array_index]);
                created_threads[thread_array_index] = NULL;
                created_thread_fd_args[thread_array_index] = -1;

                thread_creation_succeeded = false;
            }
        }
    }
}
#else  // if using thread pool

typedef enum thread_state
{
    THREAD_STATE_INITIAL,
    THREAD_STATE_RUNNING,
    THREAD_STATE_AWAITING_TASK,
    THREAD_STATE_PREPARE_FOR_TASK,
    THREAD_STATE_STOPPED,
} thread_state_t;

static volatile thread_state_t current_thread_states[NUM_THREADS];
static volatile int created_thread_fd_args[NUM_THREADS];

static pthread_mutex_t global_thread_mutexes[NUM_THREADS];
static pthread_cond_t global_conditions[NUM_THREADS];

void* server_thread_main(void* thread_index_ptr)
{
    int thread_index = *((int *) thread_index_ptr);
    pthread_mutex_t *mutex_ptr = &global_thread_mutexes[thread_index];
    pthread_cond_t *condition_ptr = &global_conditions[thread_index];


    while(THREAD_STATE_STOPPED != current_thread_states[thread_index])
    {
        switch(current_thread_states[thread_index])
        {
            case THREAD_STATE_INITIAL:
            {
                current_thread_states[thread_index] = THREAD_STATE_PREPARE_FOR_TASK;
                break;
            }
            case THREAD_STATE_PREPARE_FOR_TASK:
            {
                pthread_mutex_lock(mutex_ptr);
                created_thread_fd_args[thread_index] = -1;
                current_thread_states[thread_index] = THREAD_STATE_AWAITING_TASK;
                pthread_mutex_unlock(mutex_ptr);
            }
            case THREAD_STATE_AWAITING_TASK:
            {
                pthread_cond_wait(condition_ptr, mutex_ptr);
                pthread_mutex_unlock(mutex_ptr);

                current_thread_states[thread_index] = THREAD_STATE_RUNNING;
                break;
            }
            case THREAD_STATE_RUNNING:
            {
                DEBUG_PRINT("thread %d is running the task\n", thread_index);
                DEBUG_PRINT_IF(-1 == created_thread_fd_args[thread_index], "received wrond fd!\n");
                ServeClient(created_thread_fd_args[thread_index]);
                close(created_thread_fd_args[thread_index]);
                DEBUG_PRINT("thread %d has finished the task\n", thread_index);

                current_thread_states[thread_index] = THREAD_STATE_PREPARE_FOR_TASK;
                break;
            }
            default:
                assert(false);  // unreachable
        }
    }
    
    return NULL;
}

static bool RunServerImpl(int sockfd)
{
    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        return false;
    }

    int create_thread_result = -1;
    int thread_await_result = -1;

    pthread_t* created_threads = calloc(NUM_THREADS, sizeof(pthread_t *));
    static volatile int created_thread_index_args[NUM_THREADS];

    memset(current_thread_states, THREAD_STATE_INITIAL, sizeof(thread_state_t) * NUM_THREADS);
    memset(created_thread_fd_args, 0, sizeof(int) * NUM_THREADS);
    memset(created_thread_index_args, -1, sizeof(int) * NUM_THREADS);

    for(int i = 0; i < NUM_THREADS; i++)
    {
        pthread_mutex_init(&global_thread_mutexes[i], NULL);
        pthread_cond_init(&global_conditions[i], NULL);

        bool is_creation_successful = false;
        while(!is_creation_successful)
        {
            created_threads[i] = malloc(sizeof(pthread_t));
            memset(created_threads[i], 0, sizeof(pthread_t));

            created_thread_index_args[i] = i;
            DEBUG_PRINT("passing index %d to thread\n", created_thread_index_args[i]);
            create_thread_result = pthread_create(created_threads[i], NULL, server_thread_main, (void*) &created_thread_index_args[i]);
            if (NULL != create_thread_result)
            {
                free(created_threads[i]);
                created_threads[i] = NULL;
                created_thread_index_args[i] = -1;
                continue;
            }
            is_creation_successful = true;
        }
    }

    while (TRUE)
    {  // main accept() loop
        struct sockaddr_storage theirAddr;
        socklen_t addrSize = sizeof theirAddr;
        int newfd = accept(sockfd, (struct sockaddr*)&theirAddr, &addrSize);
        if (-1 == newfd) {
            perror("accept");
            continue;
        }

        DEBUG_PRINT("received new connection so searching for non-active thread\n");
        int thread_index = 0;
        bool found_victim_thread = false;

        while(!found_victim_thread)
        {
            pthread_mutex_lock(&global_thread_mutexes[thread_index]);
            if(THREAD_STATE_AWAITING_TASK == current_thread_states[thread_index])
            {
                found_victim_thread = true;
                DEBUG_PRINT("thread %d is not active, so assigning the task to it\n", thread_index);
            }
            pthread_mutex_unlock(&global_thread_mutexes[thread_index]);
            if(!found_victim_thread)
            {
                thread_index = (thread_index + 1) % NUM_THREADS;
            }
        }

        pthread_mutex_lock(&global_thread_mutexes[thread_index]);
        created_thread_fd_args[thread_index] = newfd;
        pthread_cond_signal(&global_conditions[thread_index]);
        pthread_mutex_unlock(&global_thread_mutexes[thread_index]);
    }
}
#endif // !(USING_THREAD_POOL)
#else  // (SHOULD_USE_THREADS)
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
#endif

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
