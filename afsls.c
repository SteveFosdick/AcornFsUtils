#include "acorn-fs.h"
#include <string.h>
#include <locale.h>

static int callback_simple(acorn_fs *fs, acorn_fs_object *obj, void *udata, const char *path)
{
    acorn_fs_info(obj, stdout);
    printf(" %s\n", path);
    return AFS_OK;
}

static int callback_print(acorn_fs *fs, acorn_fs_object *obj, void *udata, const char *path)
{
    acorn_fs_info(obj, stdout);
    printf(" %s.%s\n", (char *)udata, path);
    return AFS_OK;
}

static int callback_dodir(acorn_fs *fs, acorn_fs_object *obj, void *udata, const char *path)
{
    if (obj->is_dir)
        return fs->glob(fs, obj, "*", callback_print, (void *)path);
    else {
        acorn_fs_info(obj, stdout);
        printf(" %s\n", path);
        return AFS_OK;
    }
}

int main(int argc, char *argv[])
{
    if (--argc) {
        int status = 0;
        setlocale(LC_ALL, "");
        do {
            acorn_fs *fs;
            char *fsname = *++argv;
            char *sep = strchr(fsname, ':');
            if (sep)
                *sep++ = 0;
            if ((fs = acorn_fs_open(fsname, false))) {
                int astat;
                if (sep)
                    astat = fs->glob(fs, NULL, sep, callback_dodir, NULL);
                else
                    astat = fs->glob(fs, NULL, "*", callback_simple, NULL);
                if (astat != AFS_OK) {
                    fprintf(stderr, "afsls: %s: %s\n", fsname, acorn_fs_strerr(astat));
                    status++;
                }
            }
            else {
                fprintf(stderr, "afsls: %s: %s\n", fsname, acorn_fs_strerr(errno));
                status++;
            }
        } while (--argc);
        return status;
    }
    else {
        fputs("Usage: afsls <img-file[:pattern]> [...]\n", stderr);
        return 1;
    }
}
