#include "acorn-fs.h"
#include <string.h>
#include <locale.h>

static char path[1024];
static char *path_ptr;
static unsigned cur_depth;

static int callback(acorn_fs *fs, acorn_fs_object *obj, void *udata, unsigned depth)
{
    if (depth != cur_depth) {
        if (depth == 0) {
            path_ptr = path;
            cur_depth = depth;
        }
        else if (depth > cur_depth) {
            path_ptr += strlen(path_ptr);
            *path_ptr++ = '.';
            cur_depth = depth;
        }
        else {
            path_ptr--;
            while (cur_depth > depth) {
                *path_ptr = 0;
                path_ptr = strrchr(path, '.');
                cur_depth--;
            }
            path_ptr++;
        }
    }
    if (path_ptr + strlen(obj->name) > path + sizeof(path))
        return ENAMETOOLONG;
    acorn_fs_info(obj, stdout);
    strcpy(path_ptr, obj->name);
    printf(" %3d %s\n", depth, path);
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
                int astat;
                path_ptr = path;
                if (sep) {
                    acorn_fs_object start;
                    if ((astat = fs->find(fs, sep, &start)) == AFS_OK)
                        astat = fs->walk(fs, &start, callback, NULL);
                }
                else
                    astat = fs->walk(fs, NULL, callback, NULL);
                if (astat != AFS_OK) {
                    fprintf(stderr, "afstree: %s: %s\n", fsname, acorn_fs_strerr(errno));
                    status++;
                }
            }
            else {
                fprintf(stderr, "afstree: unable to open filesystem '%s': %s\n", fsname, acorn_fs_strerr(errno));
                status++;
            }
        } while (--argc);
    }
    else {
        fputs("Usage: afstree <img-file[:start]> [...]\n", stderr);
        return 1;
    }
}
