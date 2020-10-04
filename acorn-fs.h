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
#define AFS_CORRUPT    -8

#define AFS_ATTR_UREAD  0x0001
#define AFS_ATTR_UWRITE 0x0002
#define AFS_ATTR_UEXEC  0x0004
#define AFS_ATTR_LOCKED 0x0008
#define AFS_ATTR_OREAD  0x0010
#define AFS_ATTR_OWRITE 0x0020
#define AFS_ATTR_OEXEC  0x0040
#define AFS_ATTR_PRIV   0x0080
#define AFS_ATTR_DIR    0x0100

typedef struct {
    char          name[ACORN_FS_MAX_NAME+1];
    unsigned      load_addr;
    unsigned      exec_addr;
    unsigned      length;
    unsigned      attr;
    unsigned      sector;
    unsigned char *data;
} acorn_fs_object;

typedef struct acorn_fs acorn_fs;

typedef int (*acorn_fs_cb)(acorn_fs *fs, acorn_fs_object *obj, void *udata, const char *path);

struct acorn_fs {
    int (*find)(acorn_fs *fs, const char *adfs_name, acorn_fs_object *obj);
    int (*glob)(acorn_fs *fs, acorn_fs_object *start, const char *pattern, acorn_fs_cb cb, void *udata);
    int (*walk)(acorn_fs *fs, acorn_fs_object *start, acorn_fs_cb cb, void *udata);
    int (*load)(acorn_fs *fs, acorn_fs_object *obj);
    int (*save)(acorn_fs *fs, acorn_fs_object *obj, acorn_fs_object *dest);
    int (*check)(acorn_fs *fs, const char *fsname, FILE *mfp);
    int (*rdsect)(acorn_fs *fs, int ssect, unsigned char *buf, unsigned size);
    int (*wrsect)(acorn_fs *fs, int ssect, unsigned char *buf, unsigned size);
    int (*settitle)(acorn_fs *fs, const char *title);
    FILE *fp;
    void *priv;
    acorn_fs *next;
    char filename[1];
};

// API Functions.
extern acorn_fs *acorn_fs_open(const char *filename, bool writable);
extern int acorn_fs_close(acorn_fs *fs);
extern int acorn_fs_close_all(void);
extern const char *acorn_fs_strerr(int status);
extern void acorn_fs_free_obj(acorn_fs_object *obj);
extern int acorn_fs_info(acorn_fs_object *obj, FILE *fp);

// Internal Functions.
extern void acorn_fs_adfs_init(acorn_fs *fs);
extern void acorn_fs_dfs_init(acorn_fs *fs);
extern int acorn_fs_dfs_check(acorn_fs *fs, const char *fsname, FILE *mfp);

#endif
