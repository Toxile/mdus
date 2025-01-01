/*
 * mdus.h
 * Copyright (C) 2024 Miraculin–Daemon & Co.
 */
#pragma once

#define MESSAGE_MAX_SIZE 100000 * sizeof(char)
#define FILESIZE_STRING_MAX_LENGTH 8

void *start_request_handler(void *);
void enqueue_request(struct evhttp_request *req, void *);
