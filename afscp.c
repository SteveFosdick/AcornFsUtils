#include "acorn-fs.h"
#include <alloca.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/stat.h>

/*
 * Translate non-BBC filename characters to BBC ones according to
 * the table at http://beebwiki.mdfs.net/Filename_character_mapping
*/

static const char nativ_chars[] = "#$%&.?@^";
static const char acorn_chars[] = "?<;+/#=>";

static void name_n2a(const char *native_fn, char *acorn_fn)
{
    int ch;
    const char *ptr;
    char *end = acorn_fn + ACORN_FS_MAX_NAME;

    while ((ch = *native_fn++) && acorn_fn < end) {
        if ((ptr = strchr(nativ_chars, ch)))
            ch = acorn_chars[ptr-nativ_chars];
        *acorn_fn++ = ch;
    }
    *acorn_fn = '\0';
}

static void name_a2n(const char *acorn_fn, char *native_fn)
{
    int ch;
    const char *ptr;
    char *end = native_fn + ACORN_FS_MAX_NAME;

    while ((ch = *acorn_fn++) && native_fn < end) {
        if ((ptr = strchr(acorn_chars, ch)))
            ch = nativ_chars[ptr-acorn_chars];
        *native_fn++ = ch;
    }
    *native_fn = '\0';
}

/*
 * Handle sidecar (.inf) files containing Acorn-specific attributes
 * that cannot be stored in the native filing system.
*/

static void parse_inf(acorn_fs_object *obj, const char *filename)
{
    bool copy_name = true;
    obj->load_addr = 0;
    obj->exec_addr = 0;
    obj->length = 0;
    obj->attr = 0;

    size_t len = strlen(filename);
    char *inf = alloca(len+5);
    strcpy(inf, filename);
    strcpy(inf+len, ".inf");
    FILE *fp = fopen(inf, "r");
    if (fp) {
        int n  = fscanf(fp, "%12s%x%x%x%x", obj->name, &obj->load_addr, &obj->exec_addr, &obj->length, &obj->attr);
        if (n >= 1) {
            copy_name = false;
            if (n < 5) {
                if (n == 2)
                    obj->exec_addr = obj->load_addr;
                char lck;
                if (fscanf(fp, " %c", &lck) == 1 && (lck == 'L' || lck == 'l'))
                    obj->attr = AFS_ATTR_LOCKED;
            }
        }
        fclose(fp);
    }
    if (copy_name) {
        const char *ptr = strrchr(filename, '/');
        if (ptr)
            filename = ptr + 1;
        name_n2a(filename, obj->name);
    }
}

static int write_inf(acorn_fs_object *obj, const char *filename)
{
    size_t len = strlen(filename);
    char *inf = alloca(len+5);
    strcpy(inf, filename);
    strcpy(inf+len, ".inf");
    FILE *fp = fopen(inf, "w");
    if (fp) {
        fprintf(fp, "%-12s %08X %08X %08X %02X\n", obj->name, obj->load_addr, obj->exec_addr, obj->length, obj->attr);
        fclose(fp);
        return AFS_OK;
    }
    else {
        int err = errno;
        fprintf(stderr, "afscp: %s: %s\n", inf, strerror(err));
        return err;
    }
}

/*
 * Load a native file into memory.
*/

static int native_load(acorn_fs_object *obj, const char *filename)
{
    int status;
    parse_inf(obj, filename);
    FILE *fp = fopen(filename, "rb");
    if (fp) {
        if (!fseek(fp, 0, SEEK_END)) {
            int len = ftell(fp);
            obj->length = len;
            if (len >= 0) {
                if (len == 0) {
                    fclose(fp);
                    obj->data = NULL;
                    return AFS_OK;
                }
                else if ((obj->data = malloc(len))) {
                    if (!fseek(fp, 0, SEEK_SET)) {
                        if (fread(obj->data, len, 1, fp) == 1) {
                            fclose(fp);
                            return AFS_OK;
                        }
                        status = ferror(fp) ? errno : AFS_BAD_EOF;
                    }
                    else
                        status = errno;
                    free(obj->data);
                }
                else
                    status = errno;
            }
            else
                status = errno;
        }
        else
            status = errno;
        fclose(fp);
    }
    else
        status = errno;
    return status;
}

/*
 * Save a native file.
 */

static int native_save(acorn_fs_object *obj, const char *filename)
{
    int status;
    FILE *fp = fopen(filename, "w");
    if (fp) {
        if (fwrite(obj->data, obj->length, 1, fp) == 1)
            status = write_inf(obj, filename);
        else
            status = errno;
        fclose(fp);
    }
    else
        status = errno;
    return status;
}

static int file_a2n(acorn_fs *fs, acorn_fs_object *obj, void *udata, const char *path)
{
    int status = fs->load(fs, obj);
    if (status == AFS_OK) {
        char *dest = (char *)udata;
        size_t len = strlen(dest);
        char *name = alloca(len + ACORN_FS_MAX_NAME + 2);
        memcpy(name, dest, len);
        name[len++] = '/';
        name_a2n(obj->name, name+len);
        status = native_save(obj, name);
    }
    return status;
}

static bool is_acorn_wild(const char *name)
{
    const char *sep = strchr(name, ':');
    if (sep++)
        return strchr(sep, '#') || strchr(sep, '*');
    return 0;
}

static int acorn_dest(int argc, char **argv, const char *fsname, char *dest)
{
    int status;
    *dest++ = 0;
    acorn_fs *fs = acorn_fs_open(fsname, true);
    if (fs) {
        if (!*dest)
            dest = "$";
        acorn_fs_object dobj;
        int astat = fs->find(fs, dest, &dobj);
        if (astat == AFS_OK) {
            if (dobj.attr & AFS_ATTR_DIR || (argc == 2 && !is_acorn_wild(argv[1]))) {
            }
            else {
                fputs("afscp: destination must be a directory for multi-file copy\n", stderr);
                status = 3;
            }
        }
        else {
            fprintf(stderr, "afscp: %s: %s\n", dest, acorn_fs_strerr(errno));
            status = 2;
        }
    }
    else {
        fprintf(stderr, "afscp: %s: %s\n", dest, acorn_fs_strerr(errno));
        status = 2;
    }
    return status;
}

static int native_dest(int argc, char **argv, const char *dest)
{
    int status;
    struct stat stb;
    if (!stat(dest, &stb)) {
        if (S_ISDIR(stb.st_mode) || (argc == 2 && !is_acorn_wild(argv[1]))) {
            for (argc -= 2; argc; argc--) {
                char *item = *++argv;
                char *sep = strchr(item, ':');
                if (sep) {
                    *sep++ = 0;
                    acorn_fs *fs = acorn_fs_open(item, false);
                    if (fs)
                        status = fs->glob(fs, NULL, sep, file_a2n, (void *)dest);
                }
                else {
                    acorn_fs_object obj;
                    if ((status = native_load(&obj, item)) == AFS_OK)
                        status = native_save(&obj, item);
                }
            }
        }
        else {
            fputs("afscp: destination must be a directory for multi-file copy\n", stderr);
            status = 3;
        }
    }
    else {
        fprintf(stderr, "afscp: %s: %s\n", dest, strerror(errno));
        status = 2;
    }
    return status;
}

int main(int argc, char *argv[])
{
    int status;
    if (argc > 2) {
        const char *dest = argv[argc-1];
        printf("dest=%s\n", dest);
        char *sep = strchr(dest, ':');
        if (sep)
            status = acorn_dest(argc, argv, dest, sep);
        else
            status = native_dest(argc, argv, dest);
        acorn_fs_close_all();
    }
    else {
        fputs("Usage: afscp <src> [ <src> ... ] <dest>\n", stderr);
        status = 1;
    }
    return status;
}
