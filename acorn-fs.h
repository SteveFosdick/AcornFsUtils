#ifndef ACORN_FS_INC
#define ACORN_FS_INC

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ACORN_FS_SECT_SIZE 256
#define ACORN_FS_MAX_NAME   12
#define ACORN_FS_MAX_PATH  256

#define AFS_OK          0
#define AFS_BAD_EOF    -1
#define AFS_NOT_ACORN  -2
#define AFS_BROKEN_DIR -3
#define AFS_BAD_FSMAP  -4
#define AFS_BUG        -5
#define AFS_MAP_FULL   -6
#define AFS_DIR_FULL   -7

typedef struct {
    char          name[ACORN_FS_MAX_NAME+1];
    unsigned      user_read:1;
    unsigned      user_write:1;
    unsigned      locked:1;
    unsigned      is_dir:1;
    unsigned      user_exec:1;
    unsigned      pub_read:1;
    unsigned      pub_write:1;
    unsigned      pub_exec:1;
    unsigned      priv:1;
    unsigned      load_addr;
    unsigned      exec_addr;
    unsigned      length;
    unsigned      sector;
    unsigned char *data;
} acorn_fs_object;

typedef struct acorn_fs acorn_fs;

typedef int (*acorn_fs_cb)(acorn_fs *fs, acorn_fs_object *obj, void *udata, unsigned depth);

struct acorn_fs {
    int (*find)(acorn_fs *fs, const char *adfs_name, acorn_fs_object *obj);
    int (*glob)(acorn_fs *fs, const char *pattern, acorn_fs_cb cb, void *udata);
    int (*walk)(acorn_fs *fs, acorn_fs_object *start, acorn_fs_cb cb, void *udata);
    int (*load)(acorn_fs *fs, acorn_fs_object *obj);
    int (*save)(acorn_fs *fs, acorn_fs_object *obj, acorn_fs_object *dest);
    int (*rdsect)(acorn_fs *fs, int ssect, unsigned char *buf, unsigned size);
    int (*wrsect)(acorn_fs *fs, int ssect, unsigned char *buf, unsigned size);
    FILE *fp;
    void *priv;
    acorn_fs *next;
    char filename[1];
};

extern acorn_fs *acorn_fs_open(const char *filename, bool writable);
extern int acorn_fs_close(acorn_fs *fs);
extern int acorn_fs_close_all(void);
extern const char *acorn_fs_strerr(int status);
extern int acorn_fs_wildmat(const char *pattern, const unsigned char *candidate, size_t len);
extern void acorn_fs_free_obj(acorn_fs_object *obj);
extern int acorn_fs_info(acorn_fs_object *obj, FILE *fp);

#endif
