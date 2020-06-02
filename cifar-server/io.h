#pragma once

#include <stdbool.h>
#include <stddef.h>

bool SendAll(int sockfd, const void* data, size_t len);
bool send_with_sendfile(int sock_fd, int file_fd, int file_size);
