#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string.h>

#include "sqlite.h"

static sqlite3 *g_user_db = NULL;  // global DB handle

int db_init(const char *db_path) {
    int rc = sqlite3_open(db_path, &g_user_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[SERVER] Cannot open %s: %s\n",
                db_path, sqlite3_errmsg(g_user_db));
        return -1;
    }

    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "  username TEXT PRIMARY KEY,"
        "  password TEXT NOT NULL"
        ");";

    char *errmsg = NULL;
    rc = sqlite3_exec(g_user_db, create_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[SERVER] Failed to create users table: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    printf("[SERVER] SQLite initialized with file: %s\n", db_path);
    return 0;
}

void db_close(void) {
    if (g_user_db) {
        sqlite3_close(g_user_db);
        g_user_db = NULL;
    }
}

int db_user_exists(const char *username) {
    if (!g_user_db || !username) return 0;

    const char *sql = "SELECT 1 FROM users WHERE username = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(g_user_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[SERVER] db_user_exists: prepare failed: %s\n",
                sqlite3_errmsg(g_user_db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    int exists = (rc == SQLITE_ROW);

    sqlite3_finalize(stmt);
    return exists;
}

int db_verify_user(const char *username, const char *password) {
    if (!g_user_db || !username || !password) return 0;

    const char *sql =
        "SELECT 1 FROM users WHERE username = ? AND password = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(g_user_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[SERVER] db_verify_user: prepare failed: %s\n",
                sqlite3_errmsg(g_user_db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    int ok = (rc == SQLITE_ROW);

    sqlite3_finalize(stmt);
    return ok;
}

int db_create_user(const char *username, const char *password) {
    if (!g_user_db || !username || !password) return 0;

    const char *sql =
        "INSERT INTO users(username, password) VALUES(?, ?);";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(g_user_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[SERVER] db_create_user: prepare failed: %s\n",
                sqlite3_errmsg(g_user_db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[SERVER] db_create_user: step failed: %s\n",
                sqlite3_errmsg(g_user_db));
        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_finalize(stmt);
    return 1;
}

void db_list_users(void) {
    if (!g_user_db) {
        fprintf(stderr, "[SERVER] db_list_users: chưa init db.\n");
        return;
    }

    const char *sql = "SELECT username FROM users ORDER BY username;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(g_user_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[SERVER] db_list_users: prepare failed: %s\n",
                sqlite3_errmsg(g_user_db));
        return;
    }

    printf("[SERVER] Danh sách các user:\n");
    int has_any = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *uname = sqlite3_column_text(stmt, 0);
        printf("  - %s\n", uname ? (const char *)uname : "(null)");
        has_any = 1;
    }

    if (!has_any) {
        printf("  (no users found)\n");
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[SERVER] db_list_users: error: %s\n",
                sqlite3_errmsg(g_user_db));
    }

    sqlite3_finalize(stmt);
}

int db_list_users_to_buffer(char *buf, size_t buf_size) {
    if (!g_user_db || !buf || buf_size == 0) return 0;
    const char *sql = "SELECT username FROM users ORDER BY username;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_user_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[SERVER] db_list_users_to_buffer: prepare failed: %s\n",
                sqlite3_errmsg(g_user_db));
        return 0;
    }
    size_t used = 0;
    int count = 0;
    int n = snprintf(buf + used, buf_size - used, "Registered users:\n");
    if (n < 0) n = 0;
    if ((size_t)n >= buf_size - used) {
        sqlite3_finalize(stmt);
        return 0;
    }
    used += (size_t)n;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *uname = sqlite3_column_text(stmt, 0);
        const char *name = uname ? (const char *)uname : "(null)";

        n = snprintf(buf + used, buf_size - used, "  - %s\n", name);
        if (n < 0) n = 0;
        if ((size_t)n >= buf_size - used) {
            used = buf_size - 1;
            break;
        }
        used += (size_t)n;
        count++;
    }
    buf[used] = '\0';
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        fprintf(stderr, "[DB] db_list_users_to_buffer: step error: %s\n",
                sqlite3_errmsg(g_user_db));
    }
    sqlite3_finalize(stmt);
    return count;
}