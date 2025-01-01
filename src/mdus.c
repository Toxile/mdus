/*
 * mdus.c
 * Copyright (C) 2024 Miraculin–Daemon & Co.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>

#include "../include/main.h"
#include "../include/util.h"
#include "../include/mdus.h"

#define REQUEST_QUEUE_SIZE 16

struct request_queue {
    int top;
    struct evhttp_request *active_request[REQUEST_QUEUE_SIZE];
} queue = {-1};

static const char * const ISALIVE_MESSAGE = "true";
static const size_t ISALIVE_MESSAGE_LEN = 5;
static const size_t FILEDIR_LEN = strlen("files/");

static char *get_filesize_as_string(char * const dst, FILE *of);
static void use_empty_response(struct evhttp_request *req, int status);
static void handle_request(struct evhttp_request *req);

void enqueue_request(struct evhttp_request *req, void *) {
    pthread_mutex_lock(&active_request_lock);
    if (queue.top < REQUEST_QUEUE_SIZE) {
        ++queue.top;
        queue.active_request[queue.top] = req;
        MDUS_INFO("new request, adding to queue at position %d\n", queue.top);
    } else {
        MDUS_WARN("new request, but cannot respond due to queue overflow\n");
        pthread_mutex_unlock(&active_request_lock);
        return;
    }
    pthread_mutex_unlock(&active_request_lock);

    pthread_mutex_lock(&request_pending_lock);
    pthread_cond_signal(&request_pending_cond);
    pthread_mutex_unlock(&request_pending_lock);
}

void *start_request_handler(void *) {
    /* static thread_local int c; */
    bool init_done = false;
    for (;;) {
        pthread_mutex_lock(&request_pending_lock);
        while (!quit_requested) {
            MDUS_DEBUG("thread %lu is ready.\n", pthread_self());
            if (!init_done) {
                init_done = true;
                pthread_mutex_lock(&pool_ready_lock);
                ++pool_ready_count;
                pthread_cond_broadcast(&pool_ready_cond);
                pthread_mutex_unlock(&pool_ready_lock);
            }
            pthread_cond_wait(&request_pending_cond, &request_pending_lock);
            if (queue.top != -1 && queue.active_request[queue.top] != NULL)
                break;
        }
        pthread_mutex_unlock(&request_pending_lock);
        if (quit_requested) break;
        
        pthread_mutex_lock(&active_request_lock);
        struct evhttp_request *thread_request = queue.active_request[queue.top];
        queue.active_request[queue.top] = NULL;
        --queue.top;
        pthread_mutex_unlock(&active_request_lock);

        MDUS_DEBUG("thread %lu will handle a request.\n", pthread_self());
        handle_request(thread_request);
    }

    MDUS_DEBUG("thread %lu will terminate.\n", pthread_self());
    pthread_mutex_unlock(&active_request_lock);
    return NULL;
}

static void handle_request(struct evhttp_request *req) {

    struct evbuffer *req_buffer = evhttp_request_get_input_buffer(req);
    size_t bytes_received = evbuffer_get_length(req_buffer);
    /* 
     *  Remove preceding "/" by arithmetic.
     *  This seems unsafe, but it's not:
     *    - evhttp methods return a properly terminated string
     *    - the length is accounted for
     */
    const char *target = evhttp_request_get_uri(req);
    if (strnlen(target, 1))
        ++target;

    enum evhttp_cmd_type method = evhttp_request_get_command(req);

    FILE *requested_fp = nullptr;
    struct evbuffer *response = evbuffer_new();
    struct evkeyvalq *response_headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(response_headers, "Access-Control-Allow-Origin", "*");
    evhttp_add_header(response_headers, "Server", "Miraculin–Daemon Unciv Server");

    if (method == EVHTTP_REQ_GET) {
        MDUS_INFO("client request: GET %s\n", target);
        if (strcmp(target, "isalive") == 0) {
            evhttp_add_header(response_headers, "Content-Type", "text/plain");
            evhttp_add_header(response_headers, "Content-Length", "4");
            evbuffer_add(response, ISALIVE_MESSAGE, ISALIVE_MESSAGE_LEN);
        } else if (strncmp(target, "files/", FILEDIR_LEN) == 0) {
            if (access(target, R_OK)) {
                use_empty_response(req, 404);
                goto response_done;
            } else {
                MDUS_INFO("file exists, will try to send... ");
                requested_fp = fopen(target, "r");
                char * const save_file_size_header_value = malloc(FILESIZE_STRING_MAX_LENGTH);
                get_filesize_as_string(save_file_size_header_value, requested_fp);

                evhttp_add_header(response_headers, "Content-Type", "text/plain");
                evhttp_add_header(response_headers, "Content-Length", save_file_size_header_value);
                free(save_file_size_header_value);

                struct evbuffer_file_segment *save_file =
                    evbuffer_file_segment_new(fileno(requested_fp), 0, -1, EVBUF_FS_CLOSE_ON_FREE);

                if (evbuffer_add_file_segment(response, save_file, 0, -1) == -1) {
                    MDUS_WARN("\nfailed to add by segment?  aborting response\n");
                    use_empty_response(req, 404);
                    evbuffer_file_segment_free(save_file);
                    goto response_done;
                }

                MDUS_OK();
                evbuffer_file_segment_free(save_file);
            }
        } else {
            use_empty_response(req, 404);
            goto response_done;
        }
    } else if (method == EVHTTP_REQ_PUT) {
        if (bytes_received <= 0) {
            MDUS_WARN("empty PUT request (did we fail to receive the buffer?)\n");
            use_empty_response(req, 400);
            goto response_done;
        }
        if (strncmp(target, "files/", FILEDIR_LEN) == 0) {
            MDUS_INFO("request OK, will try to write the file... ");
            requested_fp = fopen(target, "w");
            if (requested_fp == NULL) {
                perror("Can't get file descriptor for writing");
                use_empty_response(req, 500);
                goto response_done;
            }

            if (evbuffer_write_atmost(req_buffer, fileno(requested_fp), MESSAGE_MAX_SIZE) == -1) {
                MDUS_WARN("\ncould not write buffer to descriptor; no operation\n");
                use_empty_response(req, 500);
                goto response_done;
            }
            MDUS_OK();
        } else {
            use_empty_response(req, 403);
            goto response_done;
        }
    } else {
        use_empty_response(req, 405);
        goto response_done;
    }

    record_exchange(false, evbuffer_get_length(response));
    evhttp_send_reply(req, 200, "OK", response);
response_done:

    if (requested_fp != NULL)
        fclose(requested_fp);
    record_exchange(true, bytes_received);
    evbuffer_free(response);
}

static void use_empty_response(struct evhttp_request *req, int status) {
    MDUS_INFO("sending a default response (code %d)\n", status);
    record_exchange(false, 0);
    switch (status) {
        case 405:
            evhttp_send_reply(req, 405, "Method Not Allowed", NULL);
            break;
        case 404:
            evhttp_send_reply(req, 404, "Not Found", NULL);
            break;
        case 403:
            evhttp_send_reply(req, 403, "Forbidden", NULL);
            break;
        case 500:
            evhttp_send_reply(req, 500, "Internal Error", NULL);
            break;
        default:
            evhttp_send_reply(req, 500, "Internal Error", NULL);
            break;
    }
}

static char *get_filesize_as_string(char * const dst, FILE *of) {
    fseek(of, 0, SEEK_END);
    size_t size = (size_t) ftell(of);
    fseek(of, 0, SEEK_SET);
    snprintf(dst, FILESIZE_STRING_MAX_LENGTH, "%zu", size);
    return dst;
}
