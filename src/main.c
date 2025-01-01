/* 
 * main.c
 * Copyright (C) 2024 Miraculin–Daemon & Co.
 */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include <event2/event.h>
#include <event2/thread.h>
#include <event2/http.h>

#include "../include/util.h"
#include "../include/mdus.h"
#include "../include/main.h"

#define SERVER_ADDRESS    "localhost"
#define DEFAULT_PORT      8000
#define DEFAULT_POOL_SIZE 7

int pool_ready_count = 0;
bool quit_requested = false;

pthread_mutex_t active_request_lock    = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t request_pending_lock   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  request_pending_cond   = PTHREAD_COND_INITIALIZER;
pthread_mutex_t pool_ready_lock        = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  pool_ready_cond        = PTHREAD_COND_INITIALIZER;

static int pool_size = DEFAULT_POOL_SIZE;
static int port      = DEFAULT_PORT;
static int hbtime  = 120;

static inline int is_pool_ready();
static inline void apply_configuration(int argc, char **argv);
static inline void destroy_threads(pthread_t *threads);
static void handle_signal(int, short, void *base);
static int init_threads(pthread_t *threads);

int main(int argc, char **argv) {

    struct evhttp *server;
    struct event_base *base;
    /* (declared explicitly because they allocate) */
    struct event *sigint_event;
    struct event *sigterm_event;
    struct event *timer_event;
    struct timeval timer_timeout;

    apply_configuration(argc, argv);
    evthread_use_pthreads();

    MDUS_INFO("starting server setup\n");

    MDUS_INFO("setting up event handling... ");
    if ((base = event_base_new()) == NULL) {
        MDUS_ERR("failed to initialise event handling");
        return -1;
    }

    sigint_event = evsignal_new(base, SIGINT, handle_signal, base);
    sigterm_event = evsignal_new(base, SIGTERM, handle_signal, base);
    if (hbtime != -1) {
        timer_timeout.tv_sec = hbtime;
        timer_timeout.tv_usec = 0;
        timer_event = event_new(base, -1, EV_PERSIST, on_timeout, NULL);
        evtimer_add(timer_event, &timer_timeout);
    }
    evsignal_add(sigint_event, NULL);
    evsignal_add(sigterm_event, NULL);
    init_session_logging();
    MDUS_OK();

    MDUS_INFO("creating and binding server (%s:%d)... ", SERVER_ADDRESS, port);
    if ((server = evhttp_new(base)) == NULL) {
        MDUS_ERR("failed to instantiate server\n");
        return -1;
    }
    if (evhttp_bind_socket(server, SERVER_ADDRESS, port) == -1) {
        MDUS_ERR("failed to bind to socket\n");
        return -1;
    }
    evhttp_set_gencb(server, enqueue_request, NULL);
    evhttp_set_allowed_methods(server, EVHTTP_REQ_GET | EVHTTP_REQ_PUT);
    MDUS_OK();

    MDUS_INFO("creating threads...\n");
    pthread_t threads[pool_size];
    if (!init_threads(threads)) {
        MDUS_ERR("failed to create one or more threads\n");
        return -1;
    }
    MDUS_OK();

    MDUS_INFO("setup complete; ready to handle connections.\n");
    if (flags & MDUS_DRY)
        printf("this was a dry run; terminating gracefully.");
    else if (event_base_dispatch(base) == -1) {
        MDUS_WARN("failed to start event listener; violation or early exit likely... \n");
    }

    MDUS_INFO("stopping all threads (this may take some time)... \n");
    destroy_threads(threads);
    MDUS_OK();

    MDUS_INFO("finishing server cleanup... ");
    evhttp_free(server);
    event_free(sigint_event);
    event_free(sigterm_event);
    event_free(timer_event);
    event_base_free(base);
    MDUS_OK();

    MDUS_INFO("done.  mdus will now exit.");
    return 0;
}

static inline void apply_configuration(int argc, char **argv) {

    const struct option options[] = {
        {"help",    no_argument, 0, 'h'},
        {"verbose", no_argument, &flags, flags | MDUS_VERBOSE},
        {"dry",     no_argument, &flags, flags | MDUS_DRY},
        {"port",    required_argument, 0, 'p'},
        {"version", no_argument, 0, 'V'},
        {"hbtime",  required_argument, 0, 'c'},
        {"threads", required_argument, 0, 't'},
        {"no-warn-threads", no_argument, &flags, flags | MDUS_NTW},
        {0, 0, 0, 0},
    };

    int i, c = 0;
    while ((c = getopt_long(argc, argv, "Vvhp:t:c:", options, &i)) != -1) {
        switch (c) {
            case 'c':
                int h = atoi(optarg);
                if (h < -1) {
                    MDUS_WARN("invalid argument for --hbtime, using default");
                    break;
                }
                hbtime = h;
                break;
            case 'h':
                print_usage();
                exit(0);
                break;
            case 'p':
                    int p = atoi(optarg);
                    if (p > 65535 || p < 1) {
                        MDUS_ERR("port number must be between 1–65535");
                        exit(-1);
                    }
                    if (p < 1024 && geteuid())
                        MDUS_WARN("using port number below 1024 as non-superuser\n");
                    port = p;
                break;
            case 't':
                int t = atoi(optarg);
                if (t == 0) {
                    MDUS_ERR("invalid parameter for option -t\n");
                    exit(-1);
                }  else if (t >= 64 && !(flags & MDUS_NTW)) {
                    MDUS_WARN("using a very large number of threads\n");
                    MDUS_WARN("(--no-warn-threads to suppress this warning if you know what you're doing)\n");
                }
                pool_size = atoi(optarg);
                break;
            case 'v':
                flags |= MDUS_VERBOSE;
                break;
            case 'V':
                print_version();
                exit(0);
                break;
            case '?':
                break;
            default:
                abort();
        }
    }
}

static int init_threads(pthread_t *threads) {
    for (int i = 0; i < pool_size; ++i)
        if (pthread_create(&threads[i], NULL, start_request_handler, NULL))
            return -1;
    MDUS_INFO("done, but waiting for all threads to be ready before continuing...\n");
    pthread_mutex_lock(&pool_ready_lock);
    while (!is_pool_ready())
        pthread_cond_wait(&pool_ready_cond, &pool_ready_lock);
    pthread_mutex_unlock(&pool_ready_lock);
    MDUS_INFO("all threads ready — ");
    return 0;
}

static inline void destroy_threads(pthread_t *threads) {
    pthread_mutex_lock(&active_request_lock);
    quit_requested = true;
    pthread_mutex_unlock(&active_request_lock);

    pthread_mutex_lock(&request_pending_lock);
    pthread_cond_broadcast(&request_pending_cond);
    pthread_mutex_unlock(&request_pending_lock);

    for (int i = 0; i < pool_size; ++i)
        pthread_join(threads[i], NULL);
    MDUS_INFO("all threads have stopped — ");
}

static inline int is_pool_ready() {
    MDUS_INFO("%d/%d threads ready.\n", pool_ready_count, pool_size);
    return (pool_ready_count >= pool_size);
}

static void handle_signal(int, short, void *base) {
    event_base_loopbreak(base);
    MDUS_INFO("received interrupt\n");
}
