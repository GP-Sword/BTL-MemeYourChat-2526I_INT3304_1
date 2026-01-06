#include "topic_svc.h"
#include <stdio.h>
#include <stdlib.h>
#include "../libs/common/net_utils.h"

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
    // Check if exists
    SubscriberNode *cur = t->subs;
    while(cur) {
        if (cur->sock == sock) {
            LeaveCriticalSection(&g_topics_lock);
            return;
        }
        cur = cur->next;
    }
    // Add new
    SubscriberNode *node = (SubscriberNode *)malloc(sizeof(SubscriberNode));
    node->sock = sock;
    node->next = t->subs;
    t->subs = node;
    printf("[TOPIC] Socket %d joined %s\n", (int)sock, topic_name);
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
        // printf("[TOPIC] No subscribers for %s\n", hdr->target_id);
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
        // Chỉ liệt kê các topic bắt đầu bằng "group/"
        if (strncmp(t->name, "group/", 6) == 0) {
            offset += snprintf(buf + offset, max_len - offset, " - %s\n", t->name + 6);
        }
        t = t->next;
    }
    LeaveCriticalSection(&g_topics_lock);
    return offset;
}