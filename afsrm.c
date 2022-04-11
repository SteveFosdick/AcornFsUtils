#include "acorn-fs.h"
#include <string.h>
#include <locale.h>

int main(int argc, char *argv[])
{
    if (--argc) {
        int status = 0;
        setlocale(LC_ALL, "");
        do {
            acorn_fs *fs;
            char *fsname = *++argv;
            char *sep = strchr(fsname, ':');
            if (sep) {
                *sep++ = 0;
				if ((fs = acorn_fs_open(fsname, true))) {
					int astat = fs->remove(fs, NULL, sep);
					if (astat != AFS_OK) {
						fprintf(stderr, "afsrm: %s: %s\n", fsname, acorn_fs_strerr(astat));
						status++;
					}
					acorn_fs_close_all();
				}
				else {
					fprintf(stderr, "afsrm: %s: %s\n", fsname, acorn_fs_strerr(errno));
					status++;
				}
			}
			else {
				fprintf(stderr, "afsrm: name '%s' invalid, ':' required\n", fsname);
				status++;
			}
        } while (--argc);
        return status;
    }
    else {
        fputs("Usage: afsrm <img-file:pattern> [...]\n", stderr);
        return 1;
    }
}
