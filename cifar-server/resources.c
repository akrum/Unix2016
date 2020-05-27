#include "resources.h"

#include "bmp.h"
#include "stringutils.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/**
 * Page data
 */

#define PAGE_TITLE "CIFAR Dataset Browser"

#define BUFSIZE 4096
#define CIFAR_PATH "cifar/data_batch_1.bin"
#define CIFAR_IMG_SIZE 32
#define CIFAR_BLOB_SIZE (1 + CIFAR_IMG_SIZE * CIFAR_IMG_SIZE * 3)
#define CIFAR_NUM_IMAGES 10000
#define CIFAR_TABLE_SIZE 10
#define CIFAR_IMG_PER_PAGE (CIFAR_TABLE_SIZE * CIFAR_TABLE_SIZE)
#define CIFAR_NUM_PAGES (CIFAR_NUM_IMAGES / CIFAR_IMG_PER_PAGE)

static const char* ERROR_TEMPLATE =
"<html>\n"
"<head>\n"
"  <title>%d %s</title>\n"
"</head>\n"
"<body>\n"
"  <center><h1>%d %s</h1></center>\n"
"  <hr>\n"
"  <center>cifar-server</center>\n"
"<center>""<b>" CUSTOM_LINE_FOR_WARMUP "</b>" "</center" "\n"
"</body>\n"
"</html>\n";

static void FormatErrorPageTemplate(struct TStringBuilder* sb, int code, const char* message) {
    TStringBuilder_Clear(sb);
    TStringBuilder_Sprintf(sb, ERROR_TEMPLATE, code, message, code, message);
}

void CreateErrorPage(struct THttpResponse* response, enum EHttpCode code) {
    response->Code = code;
    response->ContentType = "text/html";
    FormatErrorPageTemplate(&response->Body, code, GetReasonPhrase(code));
}

static const char* INDEX_TEMPLATE_HEADER =
"<html>\n"
"<head>\n"
"  <title>" PAGE_TITLE "</title>\n"
"  <meta charset=\"utf-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\">\n"
"  <link rel=\"stylesheet\" href=\"static/bootstrap.min.css\">\n"
"  <style>.pic { width: 48px; height: 48px; }</style>"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
"    <img src=\"static/logo_en.svg\" width=\"232\" height=\"97\" class=\"float-right\">\n"
"    <h1>" PAGE_TITLE "</h1>\n";

static const char* INDEX_TEMPLATE_FOOTER =
"  </div>\n"
"</body>\n"
"</html>\n";

void CreateIndexPage(struct THttpResponse* response, int page) {
    if (page < 0 || page >= CIFAR_NUM_PAGES) {
        CreateErrorPage(response, HTTP_NOT_FOUND);
        return;
    }
    int img = page * CIFAR_IMG_PER_PAGE;

    response->ContentType = "text/html";
    TStringBuilder_AppendCStr(&response->Body, INDEX_TEMPLATE_HEADER);
    TStringBuilder_Sprintf(&response->Body, "<h3>Page %d</h3>\n", page);
    TStringBuilder_AppendCStr(&response->Body, "<div class=\"form-group\">\n");

    TStringBuilder_AppendCStr(&response->Body, "<table>\n");
    for (int i = 0; i < CIFAR_TABLE_SIZE; ++i) {
        TStringBuilder_AppendCStr(&response->Body, "<tr>\n");
        for (int j = 0; j < CIFAR_TABLE_SIZE; ++j) {
            TStringBuilder_Sprintf(&response->Body, "<td><img class=\"pic\" src=\"images/%d.bmp\" alt=\"#%d\"></td>", img, img);
            ++img;
        }
        TStringBuilder_AppendCStr(&response->Body, "</tr>\n");
    }
    TStringBuilder_AppendCStr(&response->Body, "</table>\n");
    TStringBuilder_AppendCStr(&response->Body, "</div>\n");

    TStringBuilder_AppendCStr(&response->Body, "<div class=\"form-group\">\n");
    TStringBuilder_Sprintf(&response->Body, "<a href=\"?page=%d\" class=\"btn btn-secondary\">Previous</a>\n", (page > 0) ? page - 1 : CIFAR_NUM_PAGES - 1);
    TStringBuilder_Sprintf(&response->Body, "<a href=\"?page=%d\" class=\"btn btn-primary\">Next</a>\n", (page + 1 < CIFAR_NUM_PAGES) ? page + 1 : 0);
    TStringBuilder_AppendCStr(&response->Body, "</div>\n");

    TStringBuilder_AppendCStr(&response->Body, INDEX_TEMPLATE_FOOTER);
}

static bool Load(int n, char** data, size_t* size) {
    int fd = open(CIFAR_PATH, O_RDONLY);
    if (fd == -1) {
        return false;
    }
    if (lseek(fd, n * CIFAR_BLOB_SIZE, SEEK_SET) == -1) {
        close(fd);
        return false;
    }
    uint8_t tmpBuf[CIFAR_BLOB_SIZE];
    if (read(fd, tmpBuf, CIFAR_BLOB_SIZE) != CIFAR_BLOB_SIZE) {
        close(fd);
        return false;
    }
    bool result = BuildBmpFileData(CIFAR_IMG_SIZE, CIFAR_IMG_SIZE, tmpBuf + 1, data, size);
    // "tmpBuf + 1" to skip a CIFAR class marker
    close(fd);
    return result;
}

void SendCifarBitmap(struct THttpResponse* response, int number) {
    char* data;
    size_t size;
    if (0 <= number && number < CIFAR_NUM_IMAGES) {
        if (Load(number, &data, &size)) {
            response->ContentType = "image/bmp";
            TStringBuilder_Clear(&response->Body);
            TStringBuilder_AppendBuf(&response->Body, data, size);
            free(data);
        } else {
            CreateErrorPage(response, HTTP_INTERNAL_SERVER_ERROR);
        }
    } else {
        CreateErrorPage(response, HTTP_NOT_FOUND);
    }
}

const struct {
    const char* Extension;
    const char* MimeType;
} MIME_TYPES[] = {
    {".svg", "image/svg+xml"},
    {".css", "text/css"},
    {".txt", "text/plain"},
    {NULL, NULL},
};

static const char* GuessContentType(const char* path) {
    for (size_t i = 0; MIME_TYPES[i].Extension != NULL; ++i) {
        if (EndsWithCI(path, MIME_TYPES[i].Extension)) {
            return MIME_TYPES[i].MimeType;
        }
    }
    return NULL;
}

static bool ReadWholeFile(int fd, struct TStringBuilder* body) {
    char buf[BUFSIZE];
    while (1) {
        ssize_t ret = read(fd, buf, BUFSIZE);
        if (ret == -1) {
            return false;
        }
        if (ret == 0) { // EOF
            break;
        }
        TStringBuilder_AppendBuf(body, buf, ret);
    }
    return true;
}

void SendStaticFile(struct THttpResponse* response, const char* path) {
    printf("requested path: %s\n", path);
    char *passed_real_path = realpath(path, NULL);
    if(NULL == passed_real_path)
    {
        perror("realpath for passed:");
        CreateErrorPage(response, HTTP_NOT_FOUND);
        return;
    }
    printf("real path: %s\n", passed_real_path);

    char *static_real_path = realpath("./static", NULL);
    if(NULL == static_real_path)
    {
        perror("realpath for static:");
        CreateErrorPage(response, HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    printf("static real path: %s\n", static_real_path);

    if(strstr(passed_real_path, static_real_path) == NULL)
    {
        CreateErrorPage(response, HTTP_BAD_REQUEST);
        return;
    }

    response->ContentType = GuessContentType(path);
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        CreateErrorPage(response, HTTP_NOT_FOUND);
        return;
    }
    if (!ReadWholeFile(fd, &response->Body)) {
        CreateErrorPage(response, HTTP_INTERNAL_SERVER_ERROR);
    }
    close(fd);
}
