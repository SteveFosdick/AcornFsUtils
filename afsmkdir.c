#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "acorn-fs.h"

// dir_path is
//
// <dir1>
// <dir1>.
// <dir1>.<dir2>
// <dir1>.<dir2>.
// <dir1>.<dir2>.<dir3>
// <dir1>.<dir2>.<dir3>.
//
// etc, where the final component is the directory name to be created

static int acorn_mkdir(const char *fsname, char *dir_path)
{
    acorn_fs *fs = acorn_fs_open(fsname, true);

    if (!fs) {
        return errno;
    }

    // Strip any terminating . seperators
    int i = strlen(dir_path);
    while (i > 0 && dir_path[i - 1] == '.') {
        i--;
        dir_path[i] = 0;
    }

    // Split the path at the last directory seperator
    char *dest;
    char *name = strrchr(dir_path, '.');
    if (name) {
        // Create in a sub directory
        dest = dir_path;
        *name++ = 0;
    } else {
        // Create in the root directory
        dest = "$";
        name = dir_path;
    }

    acorn_fs_object dobj;
    int status = fs->find(fs, dest, &dobj);
    if (status == AFS_OK && dobj.attr & AFS_ATTR_DIR) {
		acorn_fs_object child;
		strncpy(child.name, name, ACORN_FS_MAX_NAME);
        status = fs->mkdir(fs, &child, &dobj);
    }

    acorn_fs_close_all();
    return status;
}

static inline void strlcpy(char *dest, const char *src, size_t len)
{
	strncpy(dest, src, --len);
	dest[len] = 0;
}

int main(int argc, char *argv[])
{
    int status = 0;
    char orig_path[1024];

    if (argc >= 2) {
        while (argc >= 2) {
            int ret;
            char *item = *++argv;
            strlcpy(orig_path, item, sizeof(orig_path));
            char *sep = strchr(item, ':');
            if (sep) {
                *sep++ = 0;
                if ((ret = acorn_mkdir(item, sep)) != AFS_OK) {
                    fputs("afsmkdir: cannot create directory: ", stderr);
                    fputs(orig_path, stderr);
                    fputs(": ", stderr);
                    fputs(acorn_fs_strerr(ret), stderr);
                    fputs("\n", stderr);
                    status = 1;
                }
            } else {
                if (mkdir(item, 0755)) {
                    fputs("afsmkdir: cannot create directory: ", stderr);
                    perror(orig_path);
                    status = 1;
                }

            }
            argc--;
        }
    } else {
        fputs("Usage: afsmkdir <directory> [ <directory> ... ]\n", stderr);
        status = 1;
    }
    return status;
}
