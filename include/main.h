/*
 * main.h
 * Copyright (C) 2024 Miraculin–Daemon & Co.
 */
#pragma once

enum {
    MDUS_VERBOSE = 1 << 0,
    MDUS_DRY     = 1 << 1,
    MDUS_INET4   = 1 << 2,
    MDUS_QUIET   = 1 << 3,
    MDUS_NTW     = 1 << 4, /* --no-thread-warning */
};

extern bool quit_requested;

extern pthread_mutex_t active_request_lock;
extern pthread_mutex_t request_pending_lock;
extern pthread_cond_t  request_pending_cond;
extern pthread_mutex_t pool_ready_lock;
extern pthread_cond_t  pool_ready_cond;
extern int pool_ready_count;
