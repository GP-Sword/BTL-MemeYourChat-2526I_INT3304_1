#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string.h>

#include "sqlite.h"

static sqlite3 *g_user_db = NULL;  // global DB handle

int db_init(const char *db_path) {
    int rc = sqlite3_open(db_path, &g_user_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Cannot open %s: %s\n",
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
        fprintf(stderr, "[DB] Failed to create users table: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    printf("[DB] SQLite initialized with file: %s\n", db_path);
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
        fprintf(stderr, "[DB] db_user_exists: prepare failed: %s\n",
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
        fprintf(stderr, "[DB] db_verify_user: prepare failed: %s\n",
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
        fprintf(stderr, "[DB] db_create_user: prepare failed: %s\n",
                sqlite3_errmsg(g_user_db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] db_create_user: step failed: %s\n",
                sqlite3_errmsg(g_user_db));
        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_finalize(stmt);
    return 1;
}
