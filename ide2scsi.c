#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int status;

    if (argc == 3) {
        const char *in_fn = argv[1];
        FILE *in_fp = fopen(in_fn, "rb");
        if (in_fp) {
            const char *out_fn = argv[2];
            FILE *out_fp = fopen(out_fn, "wb");
            if (out_fp) {
                int byte;
                status = 0;
                while ((byte = getc(in_fp)) != EOF) {
                    if (putc(byte, out_fp) == EOF) {
                        fprintf(stderr, "ide2scsi: write error on %s: %s\n", out_fn, strerror(errno));
                        status = 4;
                        break;
                    }
                    (void)getc(in_fp);
                }
                if (fclose(out_fp)) {
                    fprintf(stderr, "ide2scsi: write error on %s: %s\n", out_fn, strerror(errno));
                    status = 4;
                }
            }
            else {
                fprintf(stderr, "ide2scsi: unable to open '%s' for writing: %s\n", out_fn, strerror(errno));
                status = 3;
            }
            fclose(in_fp);
        }
        else {
            fprintf(stderr, "ide2scsi: unable to open '%s' for reading: %s\n", in_fn, strerror(errno));
            status = 2;
        }
    }
    else {
        fputs("Usage: ide2scsi <ide-file> <scsi-file>\n", stderr);
        status = 1;
    }
    return status;
}

