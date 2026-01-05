#define _CRT_SECURE_NO_WARNINGS
#include "file_svc.h"
#include "topic_svc.h"
#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct FileUpload {
    SOCKET sock;
    unsigned long long expected_size;
    unsigned long long received_size;
    FILE *fp;
    char saved_path[260];
    char topic[64];
    char sender_id[32];
    struct FileUpload *next;
} FileUpload;

static FileUpload *g_uploads = NULL;
static CRITICAL_SECTION g_uploads_lock;

void file_svc_init(void) {
    InitializeCriticalSection(&g_uploads_lock);
}

void file_svc_cleanup(void) {
    DeleteCriticalSection(&g_uploads_lock);
}

static int parse_meta(const char *payload, char *fname, unsigned long long *size) {
    const char *sep = strchr(payload, '|');
    if (!sep) return -1;
    size_t len = sep - payload;
    if (len > 255) len = 255;
    memcpy(fname, payload, len);
    fname[len] = '\0';
    *size = _strtoui64(sep + 1, NULL, 10);
    return 0;
}

void file_handle_meta(SOCKET sock, PacketHeader *hdr, const char *payload) {
    char fname[260];
    unsigned long long size;
    if (parse_meta(payload, fname, &size) != 0) return;

    printf("[FILE] Start receiving '%s' (%llu bytes) from %s -> %s\n", 
           fname, size, hdr->sender_id, hdr->target_id);

    char files_dir[260];
    history_make_paths(hdr->target_id, NULL, files_dir);
    
    char path[260];
    time_t now = time(NULL);
    snprintf(path, sizeof(path), "%s/%lld_%s", files_dir, (long long)now, fname);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        printf("[FILE] Cannot create %s\n", path);
        return;
    }

    EnterCriticalSection(&g_uploads_lock);
    FileUpload *fu = (FileUpload *)malloc(sizeof(FileUpload));
    fu->sock = sock;
    fu->expected_size = size;
    fu->received_size = 0;
    fu->fp = fp;
    strcpy(fu->saved_path, path);
    strcpy(fu->topic, hdr->target_id);
    strcpy(fu->sender_id, hdr->sender_id);
    fu->next = g_uploads;
    g_uploads = fu;
    LeaveCriticalSection(&g_uploads_lock);

    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "[FILE_META] filename=%s saved=%s size=%llu", fname, path, size);
    history_log(hdr->target_id, hdr->sender_id, "FILE", log_msg);

    topic_route_msg(sock, hdr, payload);
}

void file_handle_chunk(SOCKET sock, PacketHeader *hdr, const char *payload) {
    EnterCriticalSection(&g_uploads_lock);
    FileUpload *fu = g_uploads;
    FileUpload *prev = NULL;
    while (fu && fu->sock != sock) {
        prev = fu; fu = fu->next;
    }
    
    if (!fu) {
        LeaveCriticalSection(&g_uploads_lock);
        return;
    }

    // Ghi file
    fwrite(payload, 1, hdr->payload_size, fu->fp);
    fu->received_size += hdr->payload_size;
    int done = (fu->received_size >= fu->expected_size);
    LeaveCriticalSection(&g_uploads_lock);

    // Forward chunk
    topic_route_msg(sock, hdr, payload);

    // Dọn dẹp nếu xong
    if (done) {
        EnterCriticalSection(&g_uploads_lock);
        if (fu->fp) fclose(fu->fp);

        FileUpload *curr = g_uploads, *p = NULL;
        while(curr) {
            if (curr == fu) {
                if (p) p->next = curr->next;
                else g_uploads = curr->next;
                break;
            }
            p = curr; curr = curr->next;
        }
        printf("[FILE] Upload done: %s\n", fu->saved_path);
        free(fu);
        LeaveCriticalSection(&g_uploads_lock);
    }
}

void file_cancel_uploads(SOCKET sock) {
    EnterCriticalSection(&g_uploads_lock);
    FileUpload *curr = g_uploads;
    FileUpload *prev = NULL;
    while(curr) {
        if (curr->sock == sock) {
            if (curr->fp) fclose(curr->fp);
            if (prev) prev->next = curr->next;
            else g_uploads = curr->next;
            
            FileUpload *tmp = curr;
            curr = curr->next;
            free(tmp);
            continue;
        }
        prev = curr;
        curr = curr->next;
    }
    LeaveCriticalSection(&g_uploads_lock);
}