/*
 * user.c — User registration and login with simple hash-based authentication.
 *
 * Uses djb2 hash for passwords (NOT cryptographic — sufficient for a game).
 * User database is stored as a flat binary file.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "user.h"

/* ------------------------------------------------------------------ */
/* djb2 hash by Dan Bernstein                                         */
/* ------------------------------------------------------------------ */
unsigned int userdb_hash(const char *str)
{
    unsigned int hash = 5381;
    int c;

    while ((c = (unsigned char)*str++) != 0)
        hash = ((hash << 5) + hash) + c;   /* hash * 33 + c */

    return hash;
}

/* ------------------------------------------------------------------ */
/* Create / destroy                                                   */
/* ------------------------------------------------------------------ */
UserDB *userdb_create(void)
{
    UserDB *db = (UserDB *)calloc(1, sizeof(UserDB));
    return db;  /* all fields zeroed, count = 0 */
}

void userdb_destroy(UserDB *db)
{
    if (db)
        free(db);
}

/* ------------------------------------------------------------------ */
/* Load / save (binary)                                               */
/* ------------------------------------------------------------------ */
int userdb_load(UserDB *db, const char *filename)
{
    FILE *fp;

    if (!db || !filename)
        return 0;

    fp = fopen(filename, "rb");
    if (!fp)
        return 0;   /* file doesn't exist yet — not an error for first run */

    if (fread(db, sizeof(UserDB), 1, fp) != 1) {
        fclose(fp);
        memset(db, 0, sizeof(UserDB));
        return 0;
    }

    fclose(fp);

    /* Sanity check */
    if (db->count < 0 || db->count > MAX_USERS) {
        memset(db, 0, sizeof(UserDB));
        return 0;
    }

    return 1;
}

int userdb_save(UserDB *db, const char *filename)
{
    FILE *fp;

    if (!db || !filename)
        return 0;

    fp = fopen(filename, "wb");
    if (!fp)
        return 0;

    if (fwrite(db, sizeof(UserDB), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Register — returns 1 on success, 0 if username taken or DB full    */
/* ------------------------------------------------------------------ */
int userdb_register(UserDB *db, const char *username, const char *password)
{
    int i;

    if (!db || !username || !password)
        return 0;

    if (username[0] == '\0' || password[0] == '\0')
        return 0;

    if (db->count >= MAX_USERS)
        return 0;

    /* Check if username already taken */
    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].username, username) == 0)
            return 0;
    }

    /* Add new user */
    strncpy(db->users[db->count].username, username, MAX_USERNAME - 1);
    db->users[db->count].username[MAX_USERNAME - 1] = '\0';
    db->users[db->count].pw_hash = userdb_hash(password);
    db->count++;

    return 1;
}

/* ------------------------------------------------------------------ */
/* Login — returns 1 on success, 0 on failure                         */
/* ------------------------------------------------------------------ */
int userdb_login(UserDB *db, const char *username, const char *password)
{
    int i;
    unsigned int hash;

    if (!db || !username || !password)
        return 0;

    hash = userdb_hash(password);

    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].username, username) == 0) {
            if (db->users[i].pw_hash == hash)
                return 1;
            else
                return 0;  /* wrong password */
        }
    }

    return 0;  /* username not found */
}
