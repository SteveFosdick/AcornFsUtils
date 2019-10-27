#include "acorn-fs.h"
#include <string.h>
#include <locale.h>

static int callback(acorn_fs *fs, acorn_fs_object *obj, void *udata, unsigned depth)
{
    acorn_fs_info(obj, stdout);
    printf(" %s\n", obj->name);
    return AFS_OK;
}

static bool is_acorn_wild(const char *name)
{
    const char *sep = strchr(name, ':');
    if (sep++)
        return strchr(sep, '#') || strchr(sep, '*');
    return 0;
}

static int acorn_dest(int argc, char **argv, const char *fsname, const char *dest)
{
    acorn_fs fs = acorn_fs_open(fsname, true);
    if (fs) {
        if (!*destdir)
            destdir = "$";
        acorn_fs_object dobj;
        int astat = fs->find(fs, sep, &dobj);
        if (astat == AFS_OK) {
            if (dobj.is_dir || (argc == 2 && !is_acorn_wild(argv[1]))) {
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
    struct stat stb;
    if (!stat(dest, &stb)) {
        if (S_ISDIR(stb.st_mode) || (argc == 2 && !is_acorn_wild(argv[1]))) {
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
    if (argc == 1) {
        fputs("Usage: afscp <src> [ <src> ... ] <dest>\n", stderr);
        status = 1;
    }
    else {
        const char *dest = argv[argc-1];
        char *sep = strchr(dest, ':');
        if (sep)
            status = acorn_dest(argc, argv, dest, sep+1);
        else
            status = native_dest(argc, argv, dest);
        acorn_fs_close_all();
    }
    return status;
}
