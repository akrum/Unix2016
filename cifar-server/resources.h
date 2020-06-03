#pragma once

#include "http_response.h"
#define CUSTOM_LINE_FOR_WARMUP "Server: my custom cifar server"

void CreateErrorPage(struct THttpResponse* response, enum EHttpCode code);
void CreateIndexPage(struct THttpResponse* response, int page);
void SendCifarBitmap(struct THttpResponse* response, int number);
void SendStaticFile(struct THttpResponse* response, const char* path);
bool preload_pictures();
