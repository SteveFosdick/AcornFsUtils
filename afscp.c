#include "acorn-fs.h"
#include <alloca.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/stat.h>
#include <dirent.h>

typedef struct {
    const char      *src_fsname;
    acorn_fs        *dst_fs;
    const char      *dst_fsname;
    acorn_fs_object *dst_obj;
    const char      *dst_objname;
    bool            dst_isdir;
    bool            recurse;
} acorn_ctx;

typedef int (*native_cb)(const char *src, void *dest);

/*
 * Translate non-BBC filename characters to BBC ones according to
 * the table at http://beebwiki.mdfs.net/Filename_character_mapping
*/

static const char nativ_chars[] = "#$%&.?@^";
static const char acorn_chars[] = "?<;+/#=>";

static void name_n2a(const char *native_fn, char *acorn_fn)
{
    int ch;
    const char *ptr;
    char *end = acorn_fn + ACORN_FS_MAX_NAME;

    while ((ch = *native_fn++) && acorn_fn < end) {
        if ((ptr = strchr(nativ_chars, ch)))
            ch = acorn_chars[ptr-nativ_chars];
        *acorn_fn++ = ch;
    }
    *acorn_fn = '\0';
}

static void name_a2n(const char *acorn_fn, char *native_fn)
{
    int ch;
    const char *ptr;
    char *end = native_fn + ACORN_FS_MAX_NAME;

    while ((ch = *acorn_fn++) && native_fn < end) {
        if ((ptr = strchr(acorn_chars, ch)))
            ch = nativ_chars[ptr-acorn_chars];
        *native_fn++ = ch;
    }
    *native_fn = '\0';
}

/*
 * Handle sidecar (.inf) files containing Acorn-specific attributes
 * that cannot be stored in the native filing system.
*/

static void parse_inf(acorn_fs_object *obj, const char *filename)
{
    bool copy_name = true;
    obj->load_addr = 0;
    obj->exec_addr = 0;
    obj->length = 0;
    obj->attr = 0;

    size_t len = strlen(filename);
    char *inf = alloca(len+5);
    strcpy(inf, filename);
    strcpy(inf+len, ".inf");
    FILE *fp = fopen(inf, "r");
    if (fp) {
        int n  = fscanf(fp, "%12s%x%x%x%x", obj->name, &obj->load_addr, &obj->exec_addr, &obj->length, &obj->attr);
        if (n >= 1) {
            copy_name = false;
            if (n < 5) {
                if (n == 2)
                    obj->exec_addr = obj->load_addr;
                char lck;
                if (fscanf(fp, " %c", &lck) == 1 && (lck == 'L' || lck == 'l'))
                    obj->attr = AFS_ATTR_LOCKED;
            }
        }
        fclose(fp);
    }
    if (copy_name) {
        const char *ptr = strrchr(filename, '/');
        if (ptr)
            filename = ptr + 1;
        name_n2a(filename, obj->name);
    }
}

static int write_inf(acorn_fs_object *obj, const char *filename)
{
    size_t len = strlen(filename);
    char *inf = alloca(len+5);
    strcpy(inf, filename);
    strcpy(inf+len, ".inf");
    FILE *fp = fopen(inf, "w");
    if (fp) {
        fprintf(fp, "%-12s %08X %08X %08X %02X\n", obj->name, obj->load_addr, obj->exec_addr, obj->length, obj->attr);
        fclose(fp);
        return AFS_OK;
    }
    else {
        int err = errno;
        fprintf(stderr, "afscp: %s: %s\n", inf, strerror(err));
        return err;
    }
}

/*
 * Load a native file into memory.
*/

static int native_load(acorn_fs_object *obj, const char *filename)
{
    int status;
    parse_inf(obj, filename);
    FILE *fp = fopen(filename, "rb");
    if (fp) {
        if (!fseek(fp, 0, SEEK_END)) {
            int len = ftell(fp);
            obj->length = len;
            if (len >= 0) {
                if (len == 0) {
                    fclose(fp);
                    obj->data = NULL;
                    return AFS_OK;
                }
                else if ((obj->data = malloc(len))) {
                    if (!fseek(fp, 0, SEEK_SET)) {
                        if (fread(obj->data, len, 1, fp) == 1) {
                            fclose(fp);
                            return AFS_OK;
                        }
                        status = ferror(fp) ? errno : AFS_BAD_EOF;
                    }
                    else
                        status = errno;
                    free(obj->data);
                }
                else
                    status = errno;
            }
            else
                status = errno;
        }
        else
            status = errno;
        fclose(fp);
    }
    else
        status = errno;
    fprintf(stderr, "afscp: %s: %s\n", filename, strerror(status));
    return status;
}

/*
 * Save a native file.
 */

static int native_save(acorn_fs_object *obj, const char *filename)
{
    int status;
    FILE *fp = fopen(filename, "wb");
    if (fp) {
        if (fwrite(obj->data, obj->length, 1, fp) == 1)
            status = write_inf(obj, filename);
        else {
            status = errno;
            fprintf(stderr, "afscp: %s: %s\n", filename, strerror(status));
        }
        fclose(fp);
    }
    else {
        status = errno;
        fprintf(stderr, "afscp: %s: %s\n", filename, strerror(status));
    }
    return status;
}

static int save_file(acorn_fs_object *obj, acorn_ctx *ctx)
{
    int status;
    if (ctx->dst_fs) {
        // Acorn destination.
        if (!ctx->dst_isdir)
            strncpy(obj->name, ctx->dst_objname, ACORN_FS_MAX_NAME);
        if (!(obj->attr & (AFS_ATTR_UREAD|AFS_ATTR_UWRITE|AFS_ATTR_UEXEC|AFS_ATTR_OREAD|AFS_ATTR_OWRITE|AFS_ATTR_OEXEC)))
            obj->attr |= AFS_ATTR_UREAD|AFS_ATTR_UWRITE;
        status = ctx->dst_fs->save(ctx->dst_fs, obj, ctx->dst_obj);
        if (status != AFS_OK) {
            if (ctx->dst_isdir)
                fprintf(stderr, "afscp: %s:%s.%s: %s\n", ctx->dst_fsname, ctx->dst_objname, obj->name, acorn_fs_strerr(status));
            else
                fprintf(stderr, "afscp: %s:%s: %s\n", ctx->dst_fsname, ctx->dst_objname, acorn_fs_strerr(status));
        }
    }
    else {
        // Native destination.
        const char *name = ctx->dst_objname;
        if (ctx->dst_isdir) {
            size_t len = strlen(ctx->dst_objname);
            char *path = alloca(len + ACORN_FS_MAX_NAME + 2);
            memcpy(path, ctx->dst_objname, len);
            path[len++] = '/';
            name_a2n(obj->name, path+len);
            name = path;
        }
        status = native_save(obj, name);
    }
    return status;
}

static int acorn_src(acorn_fs *fs, acorn_fs_object *obj, void *udata, const char *path)
{
    acorn_ctx *ctx = udata;
    int astat;
    if (obj->attr & AFS_ATTR_DIR) {
		if (ctx->recurse) {
			if (ctx->dst_fs) {
				/* Destination is an Acorn filesystem */
				acorn_fs_object child;
				memcpy(child.name, obj->name, ACORN_FS_MAX_NAME);
				astat = ctx->dst_fs->mkdir(ctx->dst_fs, &child, ctx->dst_obj);
				if (astat == AFS_OK || (astat == EEXIST && (child.attr & AFS_ATTR_DIR))) {
					acorn_ctx cctx = *ctx;
					cctx.dst_objname = obj->name;
					cctx.dst_obj = &child;
					astat = fs->glob(fs, obj, "*", acorn_src, &cctx);
				}
				else
					fprintf(stderr, "afscp: unable to create Acorn directory %s: %s\n", child.name, acorn_fs_strerr(astat));
			}
			else {
				/* Destination is the native filesystem */
				size_t plen = strlen(ctx->dst_objname);
				size_t nlen = strlen(obj->name);
				char *cpath = alloca(plen+nlen+2);
				memcpy(cpath, ctx->dst_objname, plen);
				cpath[plen] = '/';
				name_a2n(obj->name, cpath+plen+1);
				printf("native destination, path=%s, cpath=%s\n", path, cpath);
				struct stat stb;
				if ((!stat(cpath, &stb) && S_ISDIR(stb.st_mode)) || !mkdir(cpath, 0755)) {
					acorn_ctx cctx = *ctx;
					cctx.dst_objname = cpath;
					astat = fs->glob(fs, obj, "*", acorn_src, &cctx);
				}
				else {
					astat = errno;
					fprintf(stderr, "afscp: unable to create native directory %s: %s\n", cpath, strerror(astat));
				}
			}	
		}
		else {
			fprintf(stderr, "afscp: skipping directory %s:%s\n", ctx->src_fsname, path);
			astat = AFS_OK;
		}
    }
    else {
		astat = fs->load(fs, obj);
		if (astat == AFS_OK)
			astat = save_file(obj, ctx);
	}
    return astat;
}

static int native_dir(const char *path, acorn_ctx *ctx);

static int native_src(const char *path, const char *name, acorn_ctx *ctx)
{
	int astat;
	struct stat stb;
	if (!stat(path, &stb)) {
		if (S_ISDIR(stb.st_mode)) {
			/* Native source is a directory */
			if (ctx->recurse) {
				if (ctx->dst_fs) {
					/* Destination is an Acorn filesystem */
					acorn_fs_object child;
					name_n2a(name, child.name);
					astat = ctx->dst_fs->mkdir(ctx->dst_fs, &child, ctx->dst_obj);
					if (astat == AFS_OK || (astat == EEXIST && (child.attr & AFS_ATTR_DIR))) {
						acorn_ctx cctx = *ctx;
						cctx.dst_objname = name;
						cctx.dst_obj = &child;
						astat = native_dir(path, &cctx);
					}
					else
						fprintf(stderr, "afscp: unable to create Acorn directory %s: %s\n", child.name, acorn_fs_strerr(astat));
				}
				else {
					/* Destination is the native filesystem */
					size_t plen = strlen(ctx->dst_objname);
					size_t nlen = strlen(name);
					char *cpath = alloca(plen+nlen+2);
					memcpy(cpath, ctx->dst_objname, plen);
					cpath[plen] = '/';
					strcpy(cpath+plen+1, name);
					printf("native destination, path=%s, cpath=%s\n", path, cpath);
					struct stat stb;
					if ((!stat(cpath, &stb) && S_ISDIR(stb.st_mode)) || !mkdir(cpath, 0755)) {
						acorn_ctx cctx = *ctx;
						cctx.dst_objname = cpath;
						astat = native_dir(path, &cctx);
					}
					else {
						astat = errno;
						fprintf(stderr, "afscp: unable to create native directory %s: %s\n", cpath, strerror(astat));
					}
				}	
			}
			else {
				astat = AFS_OK;
				fprintf(stderr, "adfscp: skipping directory %s in non-recursive mode\n", path);
			}
		}
		else {
			acorn_fs_object obj;
			astat = native_load(&obj, path);
			if (astat == AFS_OK)
				astat = save_file(&obj, ctx);
		}
	}
	else {
		astat = errno;
		fprintf(stderr, "afscp: %s: %s\n", path, strerror(astat));
	}
	return astat;
}

static bool is_inf(const char *path)
{
    const char *ptr = strrchr(path, '.');
    // we know if ptr is not NULL, ptr[0] is a dot, no need to test.
    return ptr && (ptr[1] == 'I' || ptr[1] == 'i') && (ptr[2] == 'N' || ptr[2] == 'n') && (ptr[3] == 'F' || ptr[3] == 'f') && !ptr[4];
}

static int native_dir(const char *path, acorn_ctx *ctx)
{
	int astat;
	DIR *dir = opendir(path);
	if (dir) {
		size_t plen = strlen(path);
		size_t nlen = plen+ACORN_FS_MAX_NAME;
		char *cpath = malloc(nlen);
		if (cpath) {
			astat = AFS_OK;
			memcpy(cpath, path, plen);
			cpath[plen] = '/';
			struct dirent *dp;
			while ((dp = readdir(dir))) {
				if (dp->d_name[0] != '.' && !is_inf(dp->d_name)) {
					size_t reqd = plen + strlen(dp->d_name) + 2;
					if (reqd > nlen) {
						char *npath = realloc(cpath, reqd);
						if (!npath) {
							astat = ENOENT;
							break;
						}
						cpath = npath;
						nlen = reqd;
					}
					strcpy(cpath+plen+1, dp->d_name);
					astat = native_src(cpath, dp->d_name, ctx);
					if (astat != AFS_OK)
						break;
				}
			}
			free(cpath);
		}
		else
			astat = ENOMEM;
		closedir(dir);
	}
	else {
		astat = errno;
		fprintf(stderr, "afscp: unable to opendir '%s': %s\n", path, strerror(astat));
	}
	return astat;
}

static int copy_loop(int argc, char **argv, acorn_ctx *ctx)
{
    int status = 0;
    for (argc -= 2; argc; argc--) {
        int astat;
        char *item = *++argv;
        char *sep = strchr(item, ':');
        if (sep) {
            *sep++ = 0;
            acorn_fs *fs = acorn_fs_open(item, false);
            if (fs) {
                ctx->src_fsname = item;
                astat = fs->glob(fs, NULL, sep, acorn_src, ctx);
            }
            else {
                astat = errno;
                fprintf(stderr, "afscp: %s: %s\n", item, acorn_fs_strerr(astat));
            }
        }
        else {
			const char *name = strchr(item, '/');
			astat = native_src(item, name ? name+1 : item, ctx);
		}
        if (astat != AFS_OK)
            status++;
    }
    return status;
}

static int acorn_dest(int argc, char **argv, const char *fsname, char *dest, bool recurse)
{
    int status;
    *dest++ = 0;
    acorn_fs *fs = acorn_fs_open(fsname, true);
    if (fs) {
        if (!*dest)
            dest = "$";
        acorn_fs_object dobj;
        acorn_ctx ctx;
        ctx.dst_fs = fs;
        ctx.dst_fsname = fsname;
        ctx.dst_obj = &dobj;
        ctx.dst_objname = dest;
        ctx.recurse = recurse;
        status = fs->find(fs, dest, &dobj);
        if (status == AFS_OK && dobj.attr & AFS_ATTR_DIR) {
            ctx.dst_isdir = true;
            status = copy_loop(argc, argv, &ctx);
        }
        else if ((status == AFS_OK || status == ENOENT) && argc == 3 && !recurse) {
            ctx.dst_isdir = false;
            status = copy_loop(argc, argv, &ctx);
        }
        else if (status == ENOENT) {
            fputs("afscp: destination must be a directory for multi-file/recursive copy\n", stderr);
            status = 3;
        }
        else {
            fprintf(stderr, "afscp: %s:%s: %s\n", fsname, dest, acorn_fs_strerr(status));
            status = 2;
        }
    }
    else {
        fprintf(stderr, "afscp: %s: %s\n", dest, acorn_fs_strerr(errno));
        status = 2;
    }
    return status;
}

static bool is_acorn_wild(const char *name)
{
    const char *sep = strchr(name, ':');
    if (sep++)
        return strchr(sep, '#') || strchr(sep, '*');
    return 0;
}

static int native_dest(int argc, char **argv, const char *dest, bool recurse)
{
    acorn_ctx ctx;
    ctx.dst_fs = NULL;
    ctx.dst_fsname = NULL;
    ctx.dst_obj = NULL;
    ctx.dst_objname = dest;
    ctx.recurse = recurse;
    struct stat stb;
    int status = stat(dest, &stb);
    if (!status && S_ISDIR(stb.st_mode)) {
        ctx.dst_isdir = true;
        status = copy_loop(argc, argv, &ctx);
    }
    else if ((!status || errno == ENOENT) && argc == 3 && !is_acorn_wild(argv[1]) && !recurse) {
        ctx.dst_isdir = false;
        status = copy_loop(argc, argv, &ctx);
    }
    else if (!status) {
        fputs("afscp: destination must be a directory for multi-file/recursive copy\n", stderr);
        status = 3;
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
    bool recurse = false;
    if (argc >= 2 && !strcmp(argv[1], "-r")) {
		recurse = true;
		--argc;
		++argv;
	}
    if (argc > 2) {
        const char *dest = argv[argc-1];
        char *sep = strchr(dest, ':');
        if (sep)
            status = acorn_dest(argc, argv, dest, sep, recurse);
        else
            status = native_dest(argc, argv, dest, recurse);
        acorn_fs_close_all();
    }
    else {
        fputs("Usage: afscp [ -r ] <src> [ <src> ... ] <dest>\n", stderr);
        status = 1;
    }
    return status;
}
