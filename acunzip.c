#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zip.h>
#include <sys/stat.h>

static int acunzip(const char *fn)
{
    int status = 0, err;
    zip_t *zip = zip_open(fn, ZIP_RDONLY, &err);
    if (zip) {
        zip_int64_t nfile = zip_get_num_entries(zip, 0);
        for (int file = 0; file < nfile; file++) {
            const char *name = zip_get_name(zip, file, 0);
            if (name[strlen(name)-1] == '/') {
                if (mkdir(name, 0777) < 0 && errno != EEXIST) {
                    fprintf(stderr, "acunzip: unable to create directory '%s': %s\n", name, strerror(errno));
                    status = 1;
                }
            }
            else {
                zip_file_t *zf = zip_fopen_index(zip, file, 0);
                if (zf) {
                    FILE *fp = fopen(name, "wb");
                    if (fp) {
                        char buffer[8192];
                        zip_int64_t nbytes, size = 0;
                        while ((nbytes = zip_fread(zf, buffer, sizeof(buffer))) > 0) {
                            fwrite(buffer, nbytes, 1, fp);
                            size += nbytes;
                        }
                        fclose(fp);
                        zip_uint16_t len;
                        const zip_uint8_t *data = zip_file_extra_field_get_by_id(zip, file, 0x4341, 0, &len, ZIP_FL_CENTRAL);
                        if (data && len >= 8) {
                            char inf[128];
                            snprintf(inf, sizeof(inf), "%s.inf", name);
                            fp = fopen(inf, "w");
                            if (fp) {
                                /* Name and load address */
                                const char *base = strrchr(name, '/');
                                if (base)
                                    ++base;
                                else
                                    base = name;
                                unsigned value = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
                                fprintf(fp, "%-12s %08X", base, value);
                                if (len >= 12) {
                                    /* Exec address */
                                    value = (data[11] << 24) | (data[10] << 16) | (data[9] << 8) | data[8];
                                    fprintf(fp, " %08X %08" PRIi64, value, size);
                                    if (len >= 16) {
                                        /* File attributes */
                                        value = (data[15] << 24) | (data[14] << 16) | (data[13] << 8) | data[12];
                                        fprintf(fp, " %08X", value);
                                    }
                                }
                                fputc('\n', fp);
                                fclose(fp);
                            }
                        }
                    }
                    else {
                        fprintf(stderr, "acunzip: unable to open %s for writing: %s\n", name, strerror(errno));
                        status = 1;
                    }
                }
                else {
                    fprintf(stderr, "acunzip: unable to extra file member %s: %s\n", name, zip_strerror(zip));
                    status = 1;
                }
            }
        }
        zip_close(zip);
    }
    else {
        zip_error_t error;
        zip_error_init_with_code(&error, err);
        fprintf(stderr, "acunzip: cannot open zip archive '%s': %s\n", fn, zip_error_strerror(&error));
        zip_error_fini(&error);
        status = 1;
    }
    return status;
}

int main(int argc, char **argv)
{
    int status = 0;

    if (--argc) {
        do {
            status += acunzip(*++argv);
        } while (--argc);
    }
    else {
        fputs("Usage: acunzip <zip-file> <...>\n", stderr);
        status = 1;
    }
    return status;
}
