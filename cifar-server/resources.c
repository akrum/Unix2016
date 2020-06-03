#include "resources.h"

#include "bmp.h"
#include "stringutils.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>

#define USING_SENDFILE 1
#define USING_MMAP_INSTEAD_READ 1
#define DEBUG_MODE 0

#if(DEBUG_MODE == 1)
#define DEBUG_PRINT(...) {do{printf(__VA_ARGS__);}while(0);}
#else
#define DEBUG_PRINT(...)
#endif

#if (USING_MMAP_INSTEAD_READ == 1)
static const char *g_mapped_pictures_addr = NULL;  // wondering why not size_t, but spec states char*
static size_t g_mapped_pictures_size = NULL;
#endif

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

static const char* DIR_OUTPUT_HEADER_TEMPLATE =
"<html>\n"
"<head>\n"
"  <title>" PAGE_TITLE "</title>\n"
"  <meta charset=\"utf-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\">\n"
"  <style>.pic { width: 48px; height: 48px; }</style>"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
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

#if (USING_MMAP_INSTEAD_READ == 1)
void unmap_gmapped_pictured()
{
    if(MAP_FAILED == g_mapped_pictures_addr)
    {
        DEBUG_PRINT("g_mapped_pictures were not mapped\n");
        return;
    }

    if(NULL == g_mapped_pictures_addr)
    {
        DEBUG_PRINT("pictures were not mapped before");
        return;
    }

    if(0 != munmap(g_mapped_pictures_addr, g_mapped_pictures_size))
    {
        perror("unmap call failed");
        return;
    }
}
#endif

#if (USING_MMAP_INSTEAD_READ == 1)
static bool Load(int n, char** data, size_t* size)
{
    if(NULL == g_mapped_pictures_addr)
    {
        int fd = open(CIFAR_PATH, O_RDONLY);
        if (-1 == fd)
        {
            perror("open error");
            return false;
        }

        struct stat file_stat_buf;
        if(stat(CIFAR_PATH, &file_stat_buf) < 0)
        {
            perror("stat error");
            return false;
        }
        g_mapped_pictures_size = file_stat_buf.st_size;

        g_mapped_pictures_addr = mmap(g_mapped_pictures_addr, file_stat_buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (MAP_FAILED == g_mapped_pictures_addr)
        {
            perror("mmap");
            close(fd);
            return false;
        }
        close(fd);       
    }

    assert(NULL != g_mapped_pictures_addr);
    
    if((n * CIFAR_BLOB_SIZE) > (int) g_mapped_pictures_size)
    {
        return false;
    }

    uint8_t tmpBuf[CIFAR_BLOB_SIZE];
    memcpy(tmpBuf, g_mapped_pictures_addr + n * CIFAR_BLOB_SIZE, CIFAR_BLOB_SIZE);
    bool result = BuildBmpFileData(CIFAR_IMG_SIZE, CIFAR_IMG_SIZE, tmpBuf + 1, data, size);
    return result;
}
#else
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
#endif

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

#if !(USING_SENDFILE == 1)
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
#endif

bool listdir(const char *name, int indent, struct THttpResponse* response)
{
    DIR *dir = opendir(name);
    struct dirent *entry;

    if (NULL == dir)
    {
        perror("opendir");
        return false;
    }

    bool is_success = true;

    for(entry = readdir(dir); entry != NULL; entry = readdir(dir)) {
        if (DT_DIR == entry->d_type) {
            char path[1024];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);

            TStringBuilder_AppendCStr(&response->Body, "<p>\n");
            for(int i = 0; i < indent; i++)
            {
                TStringBuilder_Sprintf(&response->Body, "%s", "-");
            }

            TStringBuilder_Sprintf(&response->Body, "[%s]\n", entry->d_name);
            TStringBuilder_AppendCStr(&response->Body, "</p>\n");

            is_success = listdir(path, indent + 1, response) & is_success;
            if(!is_success)
            {
                return false;
            }
        }
        else
        {
            TStringBuilder_AppendCStr(&response->Body, "<p>\n");
            for(int i = 0; i < indent; i++)
            {
                TStringBuilder_Sprintf(&response->Body, "%s", "-");
            }
            TStringBuilder_Sprintf(&response->Body,"%s\n", entry->d_name);
            TStringBuilder_AppendCStr(&response->Body, "</p>\n");
        }
    }
    closedir(dir);
    return is_success;
}

bool prepare_page_for_the_dir_listing(const char *path, const char *path_requested, struct THttpResponse* response)
{
    TStringBuilder_AppendCStr(&response->Body, DIR_OUTPUT_HEADER_TEMPLATE);
    TStringBuilder_Sprintf(&response->Body, "<h3>Dir %s listing:</h3>\n", path_requested);
    TStringBuilder_AppendCStr(&response->Body, "<div class=\"form-group\">\n");

    bool ex_result = listdir(path, 1, response);

    TStringBuilder_AppendCStr(&response->Body, "</div>\n");
    TStringBuilder_AppendCStr(&response->Body, INDEX_TEMPLATE_FOOTER);
    return ex_result;
}

int percent_url_decode(char* out, const char* in)
{
    static const char tbl[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
         0, 1, 2, 3, 4, 5, 6, 7,  8, 9,-1,-1,-1,-1,-1,-1,
        -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1
    };
    char c, v1, v2, *beg=out;
    if(in != NULL) {
        while((c=*in++) != '\0') {
            if(c == '%') {
                if((v1=tbl[(unsigned char)*in++]) < 0 || 
                   (v2=tbl[(unsigned char)*in++]) < 0) {
                    *beg = '\0';
                    return -1;
                }
                c = (v1<<4)|v2;
            }
            *out++ = c;
        }
    }
    *out = '\0';
    return 0;
}

void SendStaticFile(struct THttpResponse* response, const char* path) {
    printf("requested path: %s\n", path);

    char *path_decoded = calloc(strlen(path) + 1, sizeof(char));
    percent_url_decode(path_decoded, path);
    DEBUG_PRINT("real path decoded: %s\n", path_decoded);

    char *passed_real_path = realpath(path_decoded, NULL);
    if(NULL == passed_real_path)
    {
        perror("realpath for passed:");
        CreateErrorPage(response, HTTP_NOT_FOUND);
        return;
    }
    DEBUG_PRINT("real path: %s\n", passed_real_path);

    char *static_real_path = realpath("./static", NULL);
    if(NULL == static_real_path)
    {
        perror("realpath for static:");
        CreateErrorPage(response, HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    DEBUG_PRINT("static real path: %s\n", static_real_path);

    if(strstr(passed_real_path, static_real_path) == NULL)
    {
        CreateErrorPage(response, HTTP_BAD_REQUEST);
        return;
    }

    struct stat file_stat_buf;
    if(stat(passed_real_path, &file_stat_buf) < 0)
    {
        perror("dir stat error:");
        CreateErrorPage(response, HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    
    if(S_ISDIR(file_stat_buf.st_mode))
    {
        DEBUG_PRINT("given path is a folder\n");
        if(prepare_page_for_the_dir_listing(passed_real_path, path, response) == false)
        {
            CreateErrorPage(response, HTTP_INTERNAL_SERVER_ERROR);
        }
        return;
    }

    response->ContentType = GuessContentType(path);

    #if (USING_SENDFILE == 1)
    response->should_use_sendfile = true;
    response->file_path_requested = passed_real_path;
    response->sent_file_size = file_stat_buf.st_size;
    response->file_modification_time = file_stat_buf.st_mtime;

    // passed_real_path will be used later so can not be freed here
    free(static_real_path);
    #else
    int fd = open(passed_real_path, O_RDONLY);
    if (fd == -1) {
        perror("open file:");
        printf("fd is -1\n");
        CreateErrorPage(response, HTTP_NOT_FOUND);
        return;
    }
    if (!ReadWholeFile(fd, &response->Body)) {
        CreateErrorPage(response, HTTP_INTERNAL_SERVER_ERROR);
    }
    close(fd);
    free(passed_real_path);
    free(static_real_path);
    #endif
}
