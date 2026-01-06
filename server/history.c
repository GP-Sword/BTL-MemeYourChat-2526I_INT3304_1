#define _CRT_SECURE_NO_WARNINGS
#include "history.h"
#include <stdio.h>
#include <time.h>
#include <direct.h>
#include <string.h>
#include "../libs/common/protocol.h"
#include "../libs/common/net_utils.h"

#define DATA_DIR "resources"
#define MAX_LOG_PATH 260
#define MAX_HISTORY_LINES 50
 
// --- CÁC HÀM HELPER (ĐẶT Ở TRÊN CÙNG) ---

static void format_time_str(long long timestamp, char *out_buf, size_t buf_size) {
    time_t t = (time_t)timestamp;
    struct tm *tm_info = localtime(&t);
    strftime(out_buf, buf_size, "%H:%M:%S - %d/%m/%Y", tm_info);
}

static void strip_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void ensure_dir(const char *dir) { _mkdir(dir); }

static void get_storage_name(const char *topic, char *out_name, size_t max_len) {
    if (strncmp(topic, "group/", 6) == 0) {
        // group/abc -> group_abc
        snprintf(out_name, max_len, "group_%s", topic + 6);
    } 
    else if (strncmp(topic, "user/", 5) == 0) {
        // user/alice -> user_alice
        snprintf(out_name, max_len, "user_%s", topic + 5);
    } 
    else {
        // Giữ nguyên nếu không khớp pattern
        snprintf(out_name, max_len, "%s", topic);
    }
}

static void resolve_paths(const char *topic, char *logs_path) {
    ensure_dir(DATA_DIR);
    
    char storage_name[64];
    get_storage_name(topic, storage_name, sizeof(storage_name));

    char group_dir[MAX_LOG_PATH];
    snprintf(group_dir, sizeof(group_dir), "%s/%s", DATA_DIR, storage_name);
    ensure_dir(group_dir);

    char logs_dir[MAX_LOG_PATH];
    snprintf(logs_dir, sizeof(logs_dir), "%s/logs", group_dir);
    ensure_dir(logs_dir);

    if (logs_path) {
        snprintf(logs_path, MAX_LOG_PATH, "%s/history.log", logs_dir);
    }
}

void history_make_paths(const char *topic, char *group_name, char *files_dir) {
    ensure_dir(DATA_DIR);

    char storage_name[64];
    get_storage_name(topic, storage_name, sizeof(storage_name));
    
    if (group_name) strcpy(group_name, storage_name);

    char group_dir[MAX_LOG_PATH];
    snprintf(group_dir, sizeof(group_dir), "%s/%s", DATA_DIR, storage_name);
    ensure_dir(group_dir);

    if (files_dir) {
        snprintf(files_dir, MAX_LOG_PATH, "%s/files", group_dir);
        ensure_dir(files_dir);
    }
}

// --- HÀM GHI LOG CỤ THỂ (PHẢI ĐẶT TRƯỚC history_log) ---
static void write_log_line(const char *storage_topic, const char *logical_topic, const char *sender, const char *kind, const char *content) {
    char path[MAX_LOG_PATH];
    // Hàm resolve_paths sẽ tạo thư mục dựa trên storage_topic (Nơi lưu)
    resolve_paths(storage_topic, path); 

    FILE *f = fopen(path, "a");
    if (!f) return;
    
    time_t now = time(NULL);
    // Lưu ý: Lưu logical_topic (Target ID gốc) vào nội dung để Client biết tin này gửi cho ai
    fprintf(f, "%lld|%s|%s|%s|%s\n", (long long)now, logical_topic, sender, kind, content);
    fclose(f);
}

// --- HÀM GHI LOG CHÍNH ---
void history_log(const char *topic, const char *sender, const char *kind, const char *content) {
    // 1. Ghi log cho người nhận (Target)
    write_log_line(topic, topic, sender, kind, content);

    // 2. LOGIC MỚI: Nếu là Private Chat, ghi thêm 1 bản cho người gửi
    if (strncmp(topic, "user/", 5) == 0 && strcmp(sender, "SERVER") != 0 && strcmp(sender, "HISTORY") != 0) {
        char sender_topic[64];
        snprintf(sender_topic, sizeof(sender_topic), "user/%s", sender);
        
        // Lưu vào thư mục của Sender (sender_topic), nhưng nội dung vẫn giữ topic gốc
        write_log_line(sender_topic, topic, sender, kind, content);
    }
}

// --- HÀM ĐỌC LOG (REPLAY) ---
void history_replay(SOCKET client_sock, const char *topic) {
    char path[MAX_LOG_PATH];
    resolve_paths(topic, path);

    FILE *f = fopen(path, "r");
    if (!f) return;

    // Lấy 50 dòng cuối
    char line[1024];
    int total = 0;
    while(fgets(line, sizeof(line), f)) total++;
    rewind(f);

    int skip = (total > MAX_HISTORY_LINES) ? (total - MAX_HISTORY_LINES) : 0;
    int count = 0;

    // Buffer
    char temp_line[1024]; 
    char pretty_msg[MAX_PAYLOAD_SIZE];

    while(fgets(line, sizeof(line), f)) {
        if (count++ < skip) continue;
        
        strip_newline(line);
        strcpy(temp_line, line);

        // Format log: Time|Topic|Sender|Kind|Content
        char *token = strtok(temp_line, "|");
        if (!token) continue;
        long long ts = _strtoi64(token, NULL, 10);

        char *log_topic = strtok(NULL, "|"); // Topic
        if (!log_topic) log_topic = "Unknown";

        char *sender = strtok(NULL, "|");    // Sender
        if (!sender) sender = "Unknown";

        char *kind = strtok(NULL, "|");      // Kind
        if (!kind) kind = "MSG";

        char *content = strtok(NULL, "");    // Content
        if (!content) content = "";

        char time_str[64];
        format_time_str(ts, time_str, sizeof(time_str));

        // Format này chỉ dùng để hiển thị (pretty print) gửi về client
        // Client sẽ parse lại string này trong ParseAndAddMessage (lưu ý logic parse bên Client phải khớp)
        snprintf(pretty_msg, sizeof(pretty_msg), 
                "[HISTORY][%s] %s - %s <%s>: %s", log_topic, time_str, sender, kind, content);
        
        // Gửi Raw Data thay vì Pretty Msg để Client dễ xử lý hơn?
        // Hiện tại Client đang parse chuỗi Pretty Msg này. Để đơn giản giữ nguyên như code cũ của bạn.
        // Nhưng cách tốt nhất là gửi raw log line: timestamp|topic|sender|kind|content
        
        // SỬA LẠI: Gửi đúng format RAW để Client parse bằng logic mới trong net_logic.cpp
        // Logic mới bên Client: timestamp|sender|kind|content
        // Chúng ta nên gửi raw line (đã strip newline)
        // Tuy nhiên hàm ParseAndAddMessage bên Client đang mong đợi:
        // "[HISTORY]..." HOẶC là raw line
        
        // Để khớp với logic ParseAndAddMessage mới mà tôi đưa cho bạn (dùng stringstream parse |):
        // Chúng ta hãy gửi lại chính dòng raw line đó.
        
        // Cách 1: Gửi Pretty Msg như cũ (Client parse khó khăn hơn)
        // Cách 2: Gửi Raw Line (Client parse dễ hơn) -> KHUYÊN DÙNG
        
        // Ở đây tôi sẽ gửi Raw Line để logic ParseAndAddMessage bên Client hoạt động chuẩn xác nhất
        // Format Raw gửi đi: timestamp|sender|kind|content
        // (Lưu ý: topic đã có trong header target_id, nhưng trong log file của user/Alice thì topic lại là user/Bob)
        
        // Để an toàn nhất với code Client hiện tại (vừa sửa), ta sẽ gửi RAW string:
        // timestamp|sender|kind|content
        char raw_msg_for_client[MAX_PAYLOAD_SIZE];
        snprintf(raw_msg_for_client, sizeof(raw_msg_for_client), 
            "%lld|%s|%s|%s", ts, sender, kind, content);

        PacketHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.type = LTM_HISTORY;
        hdr.payload_size = (uint32_t)strlen(raw_msg_for_client);
        
        // QUAN TRỌNG: target_id gửi về Client phải là log_topic (Topic gốc của cuộc hội thoại)
        // Ví dụ: Đang đọc log của Alice, gặp dòng tin nhắn với Bob -> target_id = "user/Bob"
        strcpy(hdr.target_id, log_topic); 
        strcpy(hdr.sender_id, "HISTORY");

        send_all(client_sock, &hdr, sizeof(hdr));
        if (hdr.payload_size > 0) {
            send_all(client_sock, raw_msg_for_client, (int)hdr.payload_size);
        }
    }
    fclose(f);
}

int history_topic_exists_on_disk(const char *topic) {
    char storage_name[64];
    get_storage_name(topic, storage_name, sizeof(storage_name));

    char path[MAX_LOG_PATH];
    snprintf(path, sizeof(path), "%s/%s", DATA_DIR, storage_name);

    // Kiểm tra xem folder có tồn tại không
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return 1; // Có tồn tại
    }
    return 0; // Không tồn tại
}