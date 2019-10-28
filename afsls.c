#include "acorn-fs.h"
#include <string.h>
#include <locale.h>

static int callback(acorn_fs *fs, acorn_fs_object *obj, void *udata, const char *path)
{
    acorn_fs_info(obj, stdout);
    printf(" %s\n", path);
    return AFS_OK;
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
                int astat = fs->glob(fs, NULL, sep && *sep ? sep : "*", callback, NULL);
                if (astat != AFS_OK) {
                    fprintf(stderr, "afsls: %s: %s\n", fsname, acorn_fs_strerr(astat));
                    status++;
                }
                acorn_fs_close_all();
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
