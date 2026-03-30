#ifndef USER_H
#define USER_H

#define MAX_USERNAME 32
#define MAX_USERS    100

typedef struct {
    char username[MAX_USERNAME];
    unsigned int pw_hash;       /* simple hash of password */
} UserRecord;

typedef struct {
    UserRecord users[MAX_USERS];
    int count;
} UserDB;

typedef struct {
    char username[MAX_USERNAME];
    int  logged_in;
} CurrentUser;

UserDB     *userdb_create(void);
void        userdb_destroy(UserDB *db);
int         userdb_load(UserDB *db, const char *filename);
int         userdb_save(UserDB *db, const char *filename);

/* Register new user. Returns 1 on success, 0 if username taken or DB full. */
int         userdb_register(UserDB *db, const char *username, const char *password);

/* Login. Returns 1 on success, 0 on failure. */
int         userdb_login(UserDB *db, const char *username, const char *password);

/* Simple string hash (NOT cryptographic — sufficient for a game) */
unsigned int userdb_hash(const char *str);

#endif /* USER_H */
