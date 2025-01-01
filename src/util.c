/*
 * util.c
 * Copyright (C) 2024 Miraculin–Daemon & Co.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

#include <event2/buffer.h>

#include "../include/util.h"

static const char *usage_msg =
"Usage: mdus [OPTION]...\n\n"
"Options:\n"
"      --dry                dry run.  try to set up the server and return\n"
"                           0 if successful.  do not accept connections.\n"
"      --no-warn-threads    suppresses the warning for specifying an unusually high number of threads.\n"
"  -c, --hbtime [n]         print an informative hearbeat message every [n] seconds.  the default is 120.  -1 to disable.\n"
"  -V, --version            print the version number and exit.\n"
"  -h, --help               print this message and exit.\n"
"  -p, --port [port]        tells mdus to use port [port].  the default is 8080.\n"
"  -t  --threads [n]        tells mdus to use exactly [n] threads.  the default is 8.\n"
"  -v, --verbose            also print debugging messages.\n";

struct session_stats {
    atomic_size_t sent;
    atomic_size_t received;
    atomic_uint_fast32_t requests;
    atomic_uint_fast32_t responses;
} stats;

int flags = 0;

void print_usage() {
    printf("%s\n", usage_msg);
}

void print_version() {
    printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
}

void init_session_logging(void) {
    atomic_init(&stats.sent, 0);
    atomic_init(&stats.received, 0);
    atomic_init(&stats.requests, 0);
    atomic_init(&stats.responses, 0);
}

void record_exchange(bool is_request, size_t bytes) {
    if (is_request) {
        atomic_fetch_add(&stats.requests, 1);
        atomic_fetch_add(&stats.received, bytes);
    } else {
        atomic_fetch_add(&stats.responses, 1);
        atomic_fetch_add(&stats.sent, bytes);
    }
}

void on_timeout(int, short, void *) {
    printf("mdus: \e[1;35mheartbeat:\e[0;0m server is alive.  we have received %ld requests (aggregate %zu bytes) and sent %ld responses (aggregate %zu bytes).\n",
            atomic_load(&stats.requests),
            atomic_load(&stats.received),
            atomic_load(&stats.responses),
            atomic_load(&stats.sent));
}
