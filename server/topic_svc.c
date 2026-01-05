#define _CRT_SECURE_NO_WARNINGS
#include "topic_svc.h"
#include "history.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h> // Thư viện để dùng _mkdir
#include "../libs/common/net_utils.h"

#define DATA_DIR "resources"

typedef struct SubscriberNode {
    SOCKET sock;
    struct SubscriberNode *next;
} SubscriberNode;

typedef struct Topic {
    char name[64];
    SubscriberNode *subs;
    struct Topic *next;
} Topic;

static Topic *g_topics = NULL;
static CRITICAL_SECTION g_topics_lock;

void topic_svc_init(void) {
    InitializeCriticalSection(&g_topics_lock);
}

void topic_svc_cleanup(void) {
    DeleteCriticalSection(&g_topics_lock);
}

static Topic *find_topic(const char *name) {
    Topic *t = g_topics;
    while(t) {
        if (strcmp(t->name, name) == 0) return t;
        t = t->next;
    }
    return NULL;
}

static Topic *get_or_create_topic(const char *name) {
    Topic *t = find_topic(name);
    if (t) return t;

    t = (Topic *)malloc(sizeof(Topic));
    if (!t) return NULL;
    strncpy(t->name, name, 63);
    t->name[63] = '\0';
    t->subs = NULL;

    t->next = g_topics;
    g_topics = t;
    return t;
}

void topic_subscribe(SOCKET sock, const char *topic_name) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = get_or_create_topic(topic_name);
    if (!t) {
        LeaveCriticalSection(&g_topics_lock);
        return;
    }
    SubscriberNode *cur = t->subs;
    while(cur) {
        if (cur->sock == sock) {
            LeaveCriticalSection(&g_topics_lock);
            return;
        }
        cur = cur->next;
    }
    SubscriberNode *node = (SubscriberNode *)malloc(sizeof(SubscriberNode));
    node->sock = sock;
    node->next = t->subs;
    t->subs = node;
    LeaveCriticalSection(&g_topics_lock);
}

void topic_unsubscribe(SOCKET sock, const char *topic_name) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = find_topic(topic_name);
    if (!t) {
        LeaveCriticalSection(&g_topics_lock);
        return;
    }
    SubscriberNode **cur = &t->subs;
    while (*cur) {
        if ((*cur)->sock == sock) {
            SubscriberNode *tmp = *cur;
            *cur = (*cur)->next;
            free(tmp);
            break;
        }
        cur = &((*cur)->next);
    }
    LeaveCriticalSection(&g_topics_lock);
}

void topic_remove_socket(SOCKET sock) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = g_topics;
    while (t) {
        SubscriberNode **cur = &t->subs;
        while (*cur) {
            if ((*cur)->sock == sock) {
                SubscriberNode *tmp = *cur;
                *cur = (*cur)->next;
                free(tmp);
                continue;
            }
            cur = &((*cur)->next);
        }
        t = t->next;
    }
    LeaveCriticalSection(&g_topics_lock);
}

void topic_route_msg(SOCKET sender_sock, PacketHeader *hdr, const char *payload) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = find_topic(hdr->target_id);
    if (!t) {
        LeaveCriticalSection(&g_topics_lock);
        return;
    }
    SubscriberNode *cur = t->subs;
    while (cur) {
        SOCKET sock = cur->sock;
        if (sock != sender_sock) {
            send_all(sock, hdr, sizeof(PacketHeader));
            if (hdr->payload_size > 0 && payload) {
                send_all(sock, payload, (int)hdr->payload_size);
            }
        }
        cur = cur->next;
    }
    LeaveCriticalSection(&g_topics_lock);
}

int topic_exists(const char *topic_name) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = find_topic(topic_name);
    int exists = (t != NULL);
    LeaveCriticalSection(&g_topics_lock);
    return exists;
}

void topic_create(const char *topic_name) {
    EnterCriticalSection(&g_topics_lock);
    get_or_create_topic(topic_name);
    LeaveCriticalSection(&g_topics_lock);
}

int topic_get_list(char *buf, int max_len) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = g_topics;
    int offset = 0;
    offset += snprintf(buf + offset, max_len - offset, "Available Groups:\n");
    while (t && offset < max_len) {
        if (strncmp(t->name, "group/", 6) == 0) {
            offset += snprintf(buf + offset, max_len - offset, " - %s\n", t->name + 6);
        }
        t = t->next;
    }
    LeaveCriticalSection(&g_topics_lock);
    return offset;
}

// ============================================================
// LOGIC LƯU TRỮ PERSISTENCE THEO FOLDER NGƯỜI DÙNG (MỚI)
// ============================================================

// Helper: Tạo đường dẫn file subscription của riêng user
// VD: resources/Alice/subscriptions.txt
static void get_user_subs_path(const char *username, char *path) {
    _mkdir(DATA_DIR); // Tạo folder gốc resources
    
    char user_dir[260];
    snprintf(user_dir, sizeof(user_dir), "%s/%s", DATA_DIR, username);
    _mkdir(user_dir); // Tạo folder user (nếu chưa có)
    
    snprintf(path, 260, "%s/subscriptions.txt", user_dir);
}

// Helper: Kiểm tra xem topic đã có trong file của user chưa
static int is_topic_saved_in_user_file(const char *path, const char *topic) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    
    char line[256];
    int found = 0;
    while(fgets(line, sizeof(line), f)) {
        // Xóa ký tự xuống dòng
        size_t len = strlen(line);
        while(len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
            len--;
        }
        
        if (strcmp(line, topic) == 0) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

// Hàm thêm topic vào danh sách của user
void topic_persistence_add(const char *username, const char *topic) {
    // Không cần lưu global (mặc định) hoặc lưu chính mình
    if (strcmp(topic, "group/global") == 0) return;
    if (strncmp(topic, "user/", 5) == 0 && strcmp(topic + 5, username) == 0) return;

    char path[260];
    get_user_subs_path(username, path); // Lấy đường dẫn file riêng của user

    if (is_topic_saved_in_user_file(path, topic)) return;

    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s\n", topic); // Chỉ cần lưu tên topic, vì file đã nằm trong folder user
        fclose(f);
        printf("[PERSIST] Saved topic '%s' to %s\n", topic, path);
    }
}

// Hàm load danh sách topic khi user login
void topic_persistence_load(SOCKET sock, const char *username) {
    char user_topic[64];
    snprintf(user_topic, sizeof(user_topic), "user/%s", username);
    
    topic_subscribe(sock, "group/global");
    topic_subscribe(sock, user_topic);

    // --- SỬA LỖI Ở ĐÂY: Dùng memset để xóa sạch struct ---
    PacketHeader join_hdr;
    memset(&join_hdr, 0, sizeof(PacketHeader)); // Xóa rác bộ nhớ
    join_hdr.type = LTM_JOIN_GRP;
    strcpy(join_hdr.sender_id, "SERVER");
    strcpy(join_hdr.target_id, "group/global");
    
    send_all(sock, &join_hdr, sizeof(join_hdr));
    history_replay(sock, "group/global");

    char path[260];
    get_user_subs_path(username, path);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    char loaded_topics[100][64]; 
    int count = 0;

    while(fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while(len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) { line[len-1] = '\0'; len--; }
        if (len == 0) continue;

        char *topic = line;
        int already = 0;
        for(int i=0; i<count; i++) {
            if(strcmp(loaded_topics[i], topic) == 0) { already = 1; break; }
        }

        if (!already && count < 100) {
            strcpy(loaded_topics[count++], topic);
            
            printf("[PERSIST] Auto-subscribing %s to %s\n", username, topic);
            topic_subscribe(sock, topic);
            
            // Tái sử dụng header đã memset
            memset(&join_hdr, 0, sizeof(PacketHeader));
            join_hdr.type = LTM_JOIN_GRP;
            strcpy(join_hdr.sender_id, "SERVER");
            strcpy(join_hdr.target_id, topic);
            
            send_all(sock, &join_hdr, sizeof(join_hdr));
            history_replay(sock, topic);
        }
    }
    fclose(f);
}