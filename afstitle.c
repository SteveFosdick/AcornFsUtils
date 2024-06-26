#include "acorn-fs.h"

int main(int argc, char *argv[])
{
    if (argc == 3) {
        int status = 0;
        const char *fsname = *++argv;
        acorn_fs *fs = acorn_fs_open(fsname, true);
        if (fs) {
            const char *title = *++argv;
            int astat = fs->settitle(fs, title);
            if (astat != AFS_OK) {
                status++;
            }
        }
        acorn_fs_close_all();
        return status;
    }
    else {
        fputs("Usage: afstitle <acorn-fs-image> <title>\n", stderr);
        return 1;
    }
}
