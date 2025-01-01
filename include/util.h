/*
 * util.h
 * Copyright (C) 2024 Miraculin–Daemon & Co.
 */
#pragma once

#define PROGRAM_NAME "mdus"
#define PROGRAM_VERSION "0.0.1"

#define MDUS_DEBUG(fmt, ...) do { if (flags & MDUS_VERBOSE) printf(PROGRAM_NAME ": \e[0;36mdebug:\e[0;0m " fmt, ##__VA_ARGS__); } while (0)
#define MDUS_INFO(fmt, ...) printf(PROGRAM_NAME ": \e[1minfo:\e[0m " fmt, ##__VA_ARGS__)
#define MDUS_WARN(fmt, ...) printf(PROGRAM_NAME ": \e[0;33mwarning:\e[0;0m " fmt, ##__VA_ARGS__)
#define MDUS_ERR(fmt, ...)  printf(PROGRAM_NAME ": \e[0;31mfatal error:\e[0;0m " fmt, ##__VA_ARGS__)
#define MDUS_OK() printf("\e[1;32mOK\e[0;0m\n")

struct session_stats;
extern int flags;

void init_session_logging();
void record_exchange(bool is_request, size_t bytes);
void on_timeout(int, short, void *p);
void print_usage();
void print_version();
