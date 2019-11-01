#include "acorn-fs.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static acorn_fs *open_list;

static int check_adfs(FILE *fp, unsigned off1, unsigned off2, const char *pattern, size_t len)
{
    char id1[10], id2[10];
    if (fseek(fp, off1, SEEK_SET))
        return errno;
    if (fread(id1, len, 1, fp) != 1)
        return ferror(fp) ? errno : AFS_BAD_EOF;
    if (memcmp(id1+1, pattern, len-1))
        return AFS_NOT_ACORN;
    if (fseek(fp, off2, SEEK_SET))
        return errno;
    if (fread(id2, len, 1, fp) != 1)
        return ferror(fp) ? errno : AFS_BAD_EOF;
    if (memcmp(id1, id2, len))
        return AFS_BROKEN_DIR;
    return AFS_OK;
}

static int rdsect_simple(acorn_fs *fs, int ssect, unsigned char *buf, unsigned size)
{
    if (fseek(fs->fp, ssect * ACORN_FS_SECT_SIZE, SEEK_SET))
        return errno;
    if (fread(buf, size, 1, fs->fp) != 1)
        return ferror(fs->fp) ? errno : AFS_BAD_EOF;
    return AFS_OK;
}

static int wrsect_simple(acorn_fs *fs, int ssect, unsigned char *buf, unsigned size)
{
    if (fseek(fs->fp, ssect * ACORN_FS_SECT_SIZE, SEEK_SET))
        return errno;
    if (fwrite(buf, size, 1, fs->fp) != 1)
        return errno;
    return AFS_OK;
}

static int rdsect_ide(acorn_fs *fs, int ssect, unsigned char *buf, unsigned size)
{
    unsigned char tbuf[2*ACORN_FS_SECT_SIZE];

    if (fseek(fs->fp, ssect * ACORN_FS_SECT_SIZE * 2, SEEK_SET))
        return errno;
    while (size) {
        unsigned chunk = size;
        if (chunk > ACORN_FS_SECT_SIZE)
            chunk = ACORN_FS_SECT_SIZE;
        if (fread(tbuf, chunk * 2, 1, fs->fp) == 1) {
            unsigned char *ptr = tbuf;
            unsigned char *end = buf + chunk;
            while (buf < end) {
                *buf++ = *ptr;
                ptr += 2;
            }
            size -= chunk;
        }
        else
            return ferror(fs->fp) ? errno : AFS_BAD_EOF;
    }
    return AFS_OK;
}

static int wrsect_ide(acorn_fs *fs, int ssect, unsigned char *buf, unsigned size)
{
    unsigned char tbuf[2*ACORN_FS_SECT_SIZE];

    if (fseek(fs->fp, ssect * ACORN_FS_SECT_SIZE * 2, SEEK_SET))
        return errno;
    while (size) {
        unsigned chunk = size;
        if (chunk > ACORN_FS_SECT_SIZE)
            chunk = ACORN_FS_SECT_SIZE;
        unsigned char *ptr = tbuf;
        unsigned char *end = buf + chunk;
        while (buf < end) {
            *ptr++ = *buf++;
            *ptr++ = 0;
        }
        if (fwrite(tbuf, chunk * 2, 1, fs->fp) != 1)
            return errno;
        size -= chunk;
    }
    return AFS_OK;
}

static int lock_file(FILE *fp, bool writable)
{
#ifdef WIN32
    return AFS_OK
#else
#include <fcntl.h>

    struct flock fl;
    int fd = fileno(fp);

    fl.l_type = writable ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = 0;

    do {
        if (!fcntl(fd, F_SETLKW, &fl))
            return AFS_OK;
    } while (errno == EINTR);
    return errno;
#endif
}

static void init_link(acorn_fs *fs, FILE *fp, const char *filename)
{
    fs->fp = fp;
    strcpy(fs->filename, filename);
    fs->next = open_list;
    open_list = fs;
}

acorn_fs *acorn_fs_open(const char *filename, bool writable)
{
    for (acorn_fs *fs = open_list; fs; fs = fs->next)
        if (!strcmp(fs->filename, filename))
            return fs;

    acorn_fs *fs = malloc(sizeof(acorn_fs) + strlen(filename));
    if (fs) {
        const char *mode = writable ? "rb+" : "rb";
        FILE *fp = fopen(filename, mode);
        if (fp) {
            int status = lock_file(fp, writable);
            if (status == AFS_OK) {
                if ((status = check_adfs(fp, 0x200, 0x6fa, "Hugo", 5)) == AFS_OK) {
                    fs->rdsect = rdsect_simple;
                    fs->wrsect = wrsect_simple;
                    acorn_fs_adfs_init(fs);
                    init_link(fs, fp, filename);
                    return fs;
                }
                else if (status == AFS_NOT_ACORN) {
                    if ((status = check_adfs(fp, 0x400, 0xdf4, "\0H\0u\0g\0o", 10)) == AFS_OK) {
                        fs->rdsect = rdsect_ide;
                        fs->wrsect = wrsect_ide;
                        acorn_fs_adfs_init(fs);
                        init_link(fs, fp, filename);
                        return fs;
                    }
                }
                if (status == AFS_NOT_ACORN || status == AFS_BAD_EOF) {
                    unsigned char *dir = malloc(0x200);
                    if (dir) {
                        if (fseek(fp, 0L, SEEK_SET) == 0) {
                            if (fread(dir, 0x200, 1, fp) == 1) {
                                fs->priv = dir;
                                if (acorn_fs_dfs_check(fs, NULL, NULL) == AFS_OK) {
                                    fs->rdsect = rdsect_simple;
                                    fs->wrsect = wrsect_simple;
                                    acorn_fs_dfs_init(fs);
                                    init_link(fs, fp, filename);
                                    return fs;
                                }
                            }
                            else if (!ferror(fp))
                                errno = AFS_BAD_EOF;
                        }
                    }
                }
            }
            fclose(fp);
            errno = status;
        }
    }
    return NULL;
}

static int close_fs(acorn_fs *fs)
{
    int status = AFS_OK;
    if (fs->priv)
        free(fs->priv);
    if (fs->fp)
        if (fclose(fs->fp))
            status = errno;
    free(fs);
    return status;
}

int acorn_fs_close(acorn_fs *fs)
{
    if (fs == open_list) {
        open_list = fs->next;
        return close_fs(fs);
    }
    else {
        acorn_fs *prev = open_list;
        acorn_fs *cur = prev->next;
        while (cur) {
            if (fs == cur) {
                prev->next = cur->next;
                return close_fs(fs);
            }
            prev = cur;
            cur = cur->next;
        }
    }
    return AFS_OK;
}

int acorn_fs_close_all(void)
{
    int status = AFS_OK;
    acorn_fs *ent = open_list;
    while (ent) {
        acorn_fs *next = ent->next;
        int result = close_fs(ent);
        if (result != AFS_OK)
            status = result;
        ent = next;
    }
    open_list = NULL;
    return status;
}

static const char *msgs[] = {
    /* AFS_BAD_EOF    */ "Unexpected EOF on disc image",
    /* AFS_NOT_ACORN  */ "Not a recognised Acorn filing system",
    /* AFS_BROKEN_DIR */ "Broken directory",
    /* AFS_BAD_FSMAP  */ "Bad free space map",
    /* AFS_BUG        */ "Bug in Acorn FS utils",
    /* AFS_MAP_FULL   */ "Free space map full",
    /* AFS_DIR_FULL   */ "Directory full",
    /* AFS_CORRUPT    */ "Filesystem is corrupt"
};

const char *acorn_fs_strerr(int status)
{
    if (status >= 0)
        return strerror(status);
    else {
        status = -1 - status;
        if (status < sizeof(msgs)/sizeof(char *))
            return msgs[status];
        else
            return "Unknown error";
    }
}

void acorn_fs_free_obj(acorn_fs_object *obj)
{
    if (obj->data) {
        free(obj->data);
        obj->data = NULL;
    }
}

int acorn_fs_info(acorn_fs_object *obj, FILE *fp)
{
    char str[9];
    memset(str, '-', 9);
    unsigned a = obj->attr;
    if (a & AFS_ATTR_DIR)    str[0] = 'D';
    if (a & AFS_ATTR_LOCKED) str[1] = 'L';
    if (a & AFS_ATTR_UREAD)  str[2] = 'R';
    if (a & AFS_ATTR_UWRITE) str[3] = 'W';
    if (a & AFS_ATTR_UEXEC)  str[4] = 'E';
    if (a & AFS_ATTR_OREAD)  str[5] = 'r';
    if (a & AFS_ATTR_OWRITE) str[6] = 'w';
    if (a & AFS_ATTR_OEXEC)  str[7] = 'e';
    if (a & AFS_ATTR_PRIV)   str[8] = 'P';
    return fprintf(fp, "%.9s %08X %08X %'10d %06X", str, obj->load_addr, obj->exec_addr, obj->length, obj->sector);
}
