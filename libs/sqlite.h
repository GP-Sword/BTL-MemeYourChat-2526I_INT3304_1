#ifndef SQLITE_WRAPPER_H
#define SQLITE_WRAPPER_H

#include "sqlite3.h"  // expects sqlite3.h to be reachable via include path

// Initialize the SQLite DB file (create it and the users table if needed).
// db_path example: "users.db"
int db_init(const char *db_path);

// Close the DB at shutdown.
void db_close(void);

// Return 1 if username exists, 0 otherwise (or on error).
int db_user_exists(const char *username);

// Return 1 if (username, password) pair is valid, 0 otherwise.
int db_verify_user(const char *username, const char *password);

// Create a new user. Return 1 on success, 0 on failure.
int db_create_user(const char *username, const char *password);

#endif // SQLITE_WRAPPER_H
