#include "acorn-fs.h"

int main(int argc, char *argv[])
{
    int status = 0;
    if (argc == 3) {
        const char *fsname = argv[1];
        const char *title = argv[2];
        acorn_fs *fs = acorn_fs_open(fsname, true);
        if (fs) {
            int astat = fs->settitle(fs, title);
            if (astat != AFS_OK) {
                status = 1;
            }
        }
        else {
            status = 1;
        }
    }
    else if (argc == 2) {
        const char *fsname = argv[1];
        char title[64];
        acorn_fs *fs = acorn_fs_open(fsname, false);
        if (fs && fs->gettitle) {
            int astat = fs->gettitle(fs, title, sizeof title);
            if (astat == AFS_OK)
                puts(title);
            else
                status = 1;
        }
        else
            status = 1;
    }
    else {
        fputs("Usage: afstitle <acorn-fs-image> [ <new-title> ]\n", stderr);
        status = 2;
    }
    acorn_fs_close_all();
    return status;
}
