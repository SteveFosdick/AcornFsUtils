#include "acorn-fs.h"
#include <stdlib.h>
#include <string.h>

#define ADFS_MAX_NAME 10
#define FSMAP_MAX_ENT 82
#define FSMAP_SIZE    0x200
#define DIR_HDR_SIZE  0x05
#define DIR_ENT_SIZE  0x1A
#define DIR_FTR_SIZE  0x35

typedef struct extent extent;

struct extent {
    extent   *next;
    unsigned posn;
    unsigned size;
    char *name;
};

typedef struct {
    acorn_fs *fs;
    const char *fsname;
    extent *head;
    extent *tail;
    FILE *mfp;
} check_ctx;

static inline uint32_t adfs_get32(const unsigned char *base)
{
    return base[0] | (base[1] << 8) | (base[2] << 16) | (base[3] << 24);
}

static inline uint32_t adfs_get24(const unsigned char *base)
{
    return base[0] | (base[1] << 8) | (base[2] << 16);
}

static inline void adfs_put32(unsigned char *base, uint32_t value)
{
    base[0] = value & 0xff;
    base[1] = (value >> 8) & 0xff;
    base[2] = (value >> 16) & 0xff;
    base[3] = (value >> 24) & 0xff;
}

static inline void adfs_put24(unsigned char *base, uint32_t value)
{
    base[0] = value & 0xff;
    base[1] = (value >> 8) & 0xff;
    base[2] = (value >> 16) & 0xff;
}

static void make_root(acorn_fs_object *obj)
{
    memset(obj, 0, sizeof(acorn_fs_object));
    obj->name[0] = '$';
    obj->attr = AFS_ATTR_DIR;
    obj->length = 1280;
    obj->sector = 2;
}

static int adfs_load(acorn_fs *fs, acorn_fs_object *obj)
{
    unsigned char *data = malloc(obj->length);
    if (data) {
        obj->data = data;
        return fs->rdsect(fs, obj->sector, data, obj->length);
    }
    return errno;
}

static int check_dir(acorn_fs_object *dir)
{
    unsigned char *data = dir->data;
    if (memcmp(data+1, "Hugo", 4))
        return AFS_BROKEN_DIR;
    if (memcmp(data, data + dir->length - 6, 5))
        return AFS_BROKEN_DIR;
    return AFS_OK;
}

static int ent2obj(unsigned char *ent, acorn_fs_object *obj)
{
    int i;
    for (i = 0; i < ADFS_MAX_NAME; i++) {
        unsigned c = ent[i] & 0x7f;
        if (!c || c == 0x0d)
            break;
        obj->name[i] = c;
    }
    obj->name[i] = '\0';
    unsigned a = 0;
    if (ent[0] & 0x80) a |= AFS_ATTR_UREAD;
    if (ent[1] & 0x80) a |= AFS_ATTR_UWRITE;
    if (ent[2] & 0x80) a |= AFS_ATTR_LOCKED;
    if (ent[3] & 0x80) a |= AFS_ATTR_DIR;
    if (ent[4] & 0x80) a |= AFS_ATTR_UEXEC;
    if (ent[5] & 0x80) a |= AFS_ATTR_OREAD;
    if (ent[6] & 0x80) a |= AFS_ATTR_OWRITE;
    if (ent[7] & 0x80) a |= AFS_ATTR_OEXEC;
    if (ent[8] & 0x80) a |= AFS_ATTR_PRIV;
    obj->attr      = a;
    obj->load_addr = adfs_get32(ent + 0x0a);
    obj->exec_addr = adfs_get32(ent + 0x0e);
    obj->length    = adfs_get32(ent + 0x12);
    obj->sector    = adfs_get24(ent + 0x16);
    return i;
}

static int adfs_wildmat(const char *pattern, const unsigned char *candidate, size_t len, bool is_dir)
{
    while (len) {
        int pat_ch = *(const unsigned char *)pattern++;
        if (pat_ch == '*') {
            pat_ch = *pattern;
            if (!pat_ch)
                return 0;
            if (pat_ch == '.')
                return is_dir ? 0 : 1;
            while (len) {
                if ((*candidate & 0x7f) == 0x0d)
                    return 1;
                if (!adfs_wildmat(pattern, candidate++, len--, is_dir))
                    return 0;
            }
            return 1;
        }
        else {
            int can_ch = *candidate++ & 0x7f;
            if (!pat_ch)
                return (!can_ch || can_ch == 0x0d) ? 0 : 1;
            if (!can_ch || can_ch == 0x0d)
                return pat_ch == '.' ? 0 : 1;
            if (pat_ch != '#') {
                if (pat_ch >= 'a' && pat_ch <= 'z')
                    pat_ch &= 0x5f;
                if (can_ch >= 'a' && can_ch <= 'z')
                    can_ch &= 0x5f;
                int d = pat_ch - can_ch;
                if (d)
                    return d;
            }
            len--;
        }
    }
    return 0;
}

static int search(acorn_fs *fs, acorn_fs_object *parent, acorn_fs_object *child, const char *name, unsigned char **ent_ptr)
{
    if (!(parent->attr & AFS_ATTR_DIR))
        return ENOTDIR;
    int status = adfs_load(fs, parent);
    if (status == AFS_OK) {
        if ((status = check_dir(parent)) == AFS_OK) {
            unsigned char *ent = parent->data;
            unsigned char *end = ent + parent->length - DIR_FTR_SIZE;
            if (name[0] && name[1] == '.')
                name += 2; // discard DFS directory.
            for (ent += DIR_HDR_SIZE; ent < end; ent += DIR_ENT_SIZE) {
                if (*ent == 0) {
                    *ent_ptr = ent;
                    return ENOENT;
                }
                bool is_dir = ent[3] & 0x80;
                int i = adfs_wildmat(name, ent, ADFS_MAX_NAME, is_dir);
                if (i < 0) {
                    *ent_ptr = ent;
                    return ENOENT;
                }
                if (i == 0) {
                    ent2obj(ent, child);
                    *ent_ptr = ent;
                    return AFS_OK;
                }
            }
            *ent_ptr = NULL;
            return ENOENT;
        }
        acorn_fs_free_obj(parent);
    }
    return status;
}


static int adfs_find(acorn_fs *fs, const char *adfs_name, acorn_fs_object *obj)
{
    int status;
    const char  *ptr;
    acorn_fs_object *parent, *child, *temp, a, b;
    unsigned char *ent;

    if (adfs_name[0] == '$') {
        if (adfs_name[1] == '\0') {
            make_root(obj);
            return AFS_OK;
        } else if (adfs_name[1] == '.')
            adfs_name += 2;
    }
    make_root(&a);
    parent = &a;
    child  = &b;
    while ((ptr = strchr(adfs_name, '.'))) {
        if ((status = search(fs, parent, child, adfs_name, &ent)) != AFS_OK) {
            acorn_fs_free_obj(parent);
            return status;
        }
        temp = parent;
        parent = child;
        child = temp;
        adfs_name = ptr + 1;
    }
    status = search(fs, parent, obj, adfs_name, &ent);
    acorn_fs_free_obj(parent);
    return status;
}

static int adfs_settitle(acorn_fs *fs, const char *title)
{
    // In ADFS the title is 19 characters located at offset 0xD9
    // of the last sector in the root directory.
    unsigned char buffer[ACORN_FS_SECT_SIZE];
    acorn_fs_object root;
    make_root(&root);
    int ssect = root.sector + root.length / ACORN_FS_SECT_SIZE - 1;
    fs->rdsect(fs, ssect, buffer, ACORN_FS_SECT_SIZE);
    for (int i = 0; i < 19; i++) {
        buffer[0xD9 + i] = (i < strlen(title)) ? title[i] : 0x0D;
    }
    return fs->wrsect(fs, ssect, buffer, ACORN_FS_SECT_SIZE);
}

static int glob_dir(acorn_fs *fs, acorn_fs_object *dir, const char *pattern, acorn_fs_cb cb, void *udata, char *path, unsigned path_posn)
{
    if (!*pattern)
        return AFS_OK;
    int status = adfs_load(fs, dir);
    if (status == AFS_OK) {
        if ((status = check_dir(dir)) == AFS_OK) {
            unsigned char *ent = dir->data;
            unsigned char *end = ent + dir->length - DIR_FTR_SIZE;
            char *sep = strchr(pattern, '.');
            for (ent += DIR_HDR_SIZE; ent < end; ent += DIR_ENT_SIZE) {
                if (!*ent)
                    break;
                bool is_dir = ent[3] & 0x80;
                int i = adfs_wildmat(pattern, ent, ADFS_MAX_NAME, is_dir);
                if (i < 0)
                    break;
                if (i == 0) {
                    acorn_fs_object obj;
                    unsigned copy_len = ent2obj(ent, &obj) + 1;
                    unsigned new_posn = path_posn + copy_len;
                    if (new_posn >= ACORN_FS_MAX_PATH) {
                        status = ENAMETOOLONG;
                        break;
                    }
                    memcpy(path + path_posn, obj.name, copy_len);
                    if (is_dir && sep) {
                        path[new_posn-1] = '.';
                        status = glob_dir(fs, &obj, sep+1, cb, udata, path, new_posn);
                    }
                    else
                        status = cb(fs, &obj, udata, path);
                    if (status != AFS_OK)
                        break;
                }
            }
        }
        acorn_fs_free_obj(dir);
    }
    return status;
}

static int adfs_glob(acorn_fs *fs, acorn_fs_object *start, const char *pattern, acorn_fs_cb cb, void *udata)
{
    char path[ACORN_FS_MAX_PATH];
    if (start)
        return glob_dir(fs, start, pattern, cb, udata, path, 0);
    else {
        acorn_fs_object root;
        make_root(&root);
        if (pattern[0] == '$' && pattern[1] == '.')
            pattern += 2;
        return glob_dir(fs, &root, pattern, cb, udata, path, 0);
    }
}

static int walk_dir(acorn_fs *fs, acorn_fs_object *dir, acorn_fs_cb cb, void *udata, char *path, unsigned path_posn)
{
    int status = adfs_load(fs, dir);
    if (status == AFS_OK) {
        if ((status = check_dir(dir)) == AFS_OK) {
            unsigned char *ent = dir->data;
            unsigned char *end = ent + dir->length - DIR_FTR_SIZE;
            for (ent += DIR_HDR_SIZE; ent < end; ent += DIR_ENT_SIZE) {
                acorn_fs_object obj;
                if (!*ent)
                    break;
                unsigned copy_len = ent2obj(ent, &obj) + 1;
                unsigned new_posn = path_posn + copy_len;
                if (new_posn >= ACORN_FS_MAX_PATH) {
                    status = ENAMETOOLONG;
                    break;
                }
                memcpy(path + path_posn, obj.name, copy_len);
                if ((status = cb(fs, &obj, udata, path)) != AFS_OK)
                    break;
                if (obj.attr & AFS_ATTR_DIR) {
                    path[new_posn-1] = '.';
                    if ((status = walk_dir(fs, &obj, cb, udata, path, new_posn)) != AFS_OK)
                        break;
                }
            }
        }
        acorn_fs_free_obj(dir);
    }
    return status;
}

static int adfs_walk(acorn_fs *fs, acorn_fs_object *start, acorn_fs_cb cb, void *udata)
{
    char path[ACORN_FS_MAX_PATH];
    if (start)
        return walk_dir(fs, start, cb, udata, path, 0);
    else {
        acorn_fs_object root;
        make_root(&root);
        return walk_dir(fs, &root, cb, udata, path, 0);
    }
}

static uint8_t checksum(uint8_t *base)
{
    int i = 255, c = 0;
    unsigned sum = 255;
    while (--i >= 0) {
        sum += base[i] + c;
        c = 0;
        if (sum >= 256) {
            sum &= 0xff;
            c = 1;
        }
    }
    return sum;
}

static int load_fsmap(acorn_fs *fs)
{
    int status = AFS_OK;
    if (!fs->priv) {
        unsigned char *fsmap = malloc(FSMAP_SIZE);
        if (fsmap) {
            if ((status = fs->rdsect(fs, 0, fsmap, FSMAP_SIZE)) == AFS_OK) {
                if (checksum(fsmap) == fsmap[0xff] && checksum(fsmap + 0x100) == fsmap[0x1ff]) {
                    fs->priv = fsmap;
                    return status;
                }
                else
                    status = AFS_BAD_FSMAP;
            }
            free(fsmap);
        }
        status = errno;
    }
    return status;
}

static int save_fsmap(acorn_fs *fs)
{
    unsigned char *fsmap = fs->priv;
    if (fsmap) {
        fsmap[0x0ff] = checksum(fsmap);
        fsmap[0x1ff] = checksum(fsmap + 0x100);
        return fs->wrsect(fs, 0, fsmap, FSMAP_SIZE);
    }
    return AFS_BUG;
}

static unsigned sectors(unsigned bytes)
{
    if (!bytes)
        return 0;
    return (bytes - 1) / ACORN_FS_SECT_SIZE + 1;
}

static int map_free(acorn_fs *fs, acorn_fs_object *obj)
{
    unsigned char *fsmap = fs->priv;
    unsigned char *sizes = fsmap + 0x100;
    int end = fsmap[0x1fe];
    int ent, bytes;
    uint32_t posn, size, obj_size;

    obj_size = sectors(obj->length);
    for (ent = 0; ent < end; ent += 3) {
        posn = adfs_get24(fsmap + ent);
        size = adfs_get24(sizes + ent);
        if ((posn + size) == obj->sector) { // coallesce
            size += obj_size;
            adfs_put24(sizes + ent, size);
            return AFS_OK;
        }
        if (posn > obj->sector) {
            if (end >= FSMAP_MAX_ENT * 3)
                return AFS_MAP_FULL;
            bytes = end - ent;
            memmove(fsmap + ent + 3, fsmap + ent, bytes);
            memmove(sizes + ent + 3, sizes + ent, bytes);
            break;
        }
    }
    if (end >= FSMAP_MAX_ENT * 3)
        return AFS_MAP_FULL;
    adfs_put24(fsmap + ent, obj->sector);
    adfs_put24(sizes + ent, obj_size);
    fsmap[0x1fe] += 3;
    return AFS_OK;
}

static int alloc_write(acorn_fs *fs, acorn_fs_object *obj)
{
    unsigned char *fsmap = fs->priv;
    unsigned char *sizes = fsmap + 0x100;
    int end = fsmap[0x1fe];
    int ent, bytes;
    uint32_t posn, size, obj_size;

    obj_size = sectors(obj->length);
    for (ent = 0; ent < end; ent += 3) {
        size = adfs_get24(sizes + ent);
        if (size >= obj_size) {
            posn = adfs_get24(fsmap + ent);
            obj->sector = posn;
            if (size == obj_size) { // uses exact space so kill entry.
                bytes = end - ent;
                memmove(fsmap + ent, fsmap + ent + 3, bytes);
                memmove(sizes + ent, sizes + ent + 3, bytes);
                fsmap[0x1fe] -= 3;
            } else {
                adfs_put24(fsmap + ent, posn + obj_size);
                adfs_put24(sizes + ent, size - obj_size);
            }
            return fs->wrsect(fs, posn, obj->data, obj->length);
        }
    }
    return ENOSPC;
}

static int dir_update(acorn_fs *fs, acorn_fs_object *parent, acorn_fs_object *child, unsigned char *ent)
{
    int e = 0, o = 0;
    if (child->name[0] && child->name[1] == '.')
        o += 2; // Discard DFS directory.
    while (o < ADFS_MAX_NAME) {
        int ch = child->name[o++] & 0x7f;
        if (!ch)
            break;
        ent[e++] = ch;
    }
    while (e < ADFS_MAX_NAME)
        ent[e++] = 0x0d;
    unsigned a = child->attr;
    if (a & AFS_ATTR_UREAD)  ent[0] |= 0x80;
    if (a & AFS_ATTR_UWRITE) ent[1] |= 0x80;
    if (a & AFS_ATTR_LOCKED) ent[2] |= 0x80;
    if (a & AFS_ATTR_DIR)    ent[3] |= 0x80;
    if (a & AFS_ATTR_UEXEC)  ent[4] |= 0x80;
    if (a & AFS_ATTR_OREAD)  ent[5] |= 0x80;
    if (a & AFS_ATTR_OWRITE) ent[6] |= 0x80;
    if (a & AFS_ATTR_OEXEC)  ent[7] |= 0x80;
    if (a & AFS_ATTR_PRIV)   ent[8] |= 0x80;
    adfs_put32(ent + 0x0a, child->load_addr);
    adfs_put32(ent + 0x0e, child->exec_addr);
    adfs_put32(ent + 0x12, child->length);
    adfs_put24(ent + 0x16, child->sector);
    return fs->wrsect(fs, parent->sector, parent->data, parent->length);
}

static void dir_makeslot(acorn_fs_object *parent, unsigned char *ent)
{
    unsigned char *ftr = parent->data + parent->length - DIR_FTR_SIZE;
    unsigned bytes = ftr - ent - DIR_ENT_SIZE;
    memmove(ent + DIR_ENT_SIZE, ent, bytes);
}

static int adfs_save(acorn_fs *fs, acorn_fs_object *obj, acorn_fs_object *dest)
{
    int status;
    acorn_fs_object child;
    unsigned char *ent;

    if (!(dest->attr & AFS_ATTR_DIR))
        status = ENOTDIR;
    else if ((status = load_fsmap(fs)) == AFS_OK) {
        if ((status = search(fs, dest, &child, obj->name, &ent)) == AFS_OK) {
            if ((status = map_free(fs, &child)) == AFS_OK)
                if ((status = alloc_write(fs, obj)) == AFS_OK)
                    status = dir_update(fs, dest, obj, ent);
        }
        else if (status == ENOENT) {
            if (ent == NULL)
                status = AFS_DIR_FULL;
            else {
                if ((status = alloc_write(fs, obj)) == AFS_OK) {
                    dir_makeslot(dest, ent);
                    status = dir_update(fs, dest, obj, ent);
                }
            }
        }
        if (status == AFS_OK)
            status = save_fsmap(fs);
    }
    return status;
}

static int name_cmp(const unsigned char *a, const unsigned char *b)
{
    for (int c = ADFS_MAX_NAME; c; c--) {
        int ac = *a++ & 0x5f;
        int bc = *b++ & 0x5f;
        if ((!ac || ac == 0x0d) && (!bc || bc == 0x0d))
            return 0;
        int d = ac - bc;
        if (d)
            return d;
    }
    return 0;
}

static int check_walk(check_ctx *ctx, acorn_fs_object *dir, acorn_fs_object *parent, char *path, unsigned path_len)
{
    int status = adfs_load(ctx->fs, dir);
    if (status == AFS_OK) {
        if ((status = check_dir(dir)) == AFS_OK) {
            char *pat = dir->name;
            unsigned char *ftr = dir->data + dir->length - DIR_FTR_SIZE;
            unsigned char *ent = ftr + 1;
            unsigned char *end = ent + ADFS_MAX_NAME;
            while (ent < end) {
                int pat_ch = *pat++ & 0x7f;
                int ent_ch = *ent++ & 0x7f;
                if (!pat_ch && (!ent_ch || ent_ch == 0x0d))
                    break;
                if (pat_ch != ent_ch) {
                    fprintf(ctx->mfp, "%s:%s: broken direcrory: name mismatch\n", ctx->fsname, path);
                    status = AFS_BROKEN_DIR;
                    break;
                }
            }
            unsigned ppos = adfs_get24(ftr + 0x0b);
            if (ppos != parent->sector) {
                fprintf(ctx->mfp, "%s:%s: broken direcrory: parent link incorrect\n", ctx->fsname, path);
                status = AFS_BROKEN_DIR;
            }
            unsigned char *prev = NULL;
            for (ent = dir->data + DIR_HDR_SIZE; ent < ftr; ent += DIR_ENT_SIZE) {
                if (!*ent)
                    break;
                if (prev && name_cmp(ent, prev) < 0) {
                    fprintf(ctx->mfp, "%s:%s: broken direcrory: filenames out of order\n", ctx->fsname, path);
                    status = AFS_BROKEN_DIR;
                }
                acorn_fs_object obj;
                unsigned name_len = ent2obj(ent, &obj) + 1;
                unsigned ent_len = path_len + name_len;
                char *ent_path = malloc(ent_len + 2);
                if (!ent_path) {
                    fprintf(ctx->mfp, "%s:%s: out of memory\n", ctx->fsname, path);
                    return errno;
                }
                memcpy(ent_path, path, path_len);
                ent_path[path_len] = '.';
                memcpy(ent_path + path_len + 1, obj.name, name_len);
                ent_path[ent_len+1] = 0;
                extent *new_ext = malloc(sizeof(extent));
                if (!new_ext) {
                    fprintf(ctx->mfp, "%s:%s: out of memory\n", ctx->fsname, path);
                    return errno;
                }
                new_ext->posn = obj.sector;
                new_ext->size = sectors(obj.length);
                new_ext->name = (char *)ent_path;
                extent *cur_ext = ctx->head;
                if (!cur_ext || cur_ext->posn > new_ext->posn || (cur_ext->posn == new_ext->posn && cur_ext->size > new_ext->size)) {
                    new_ext->next = cur_ext;
                    ctx->head = new_ext;
                }
                else {
                    extent *prev_ext;
                    do {
                        prev_ext = cur_ext;
                        cur_ext = cur_ext->next;
                    } while (cur_ext && (cur_ext->posn < new_ext->posn || (cur_ext->posn == new_ext->posn && cur_ext->size <= new_ext->size)));
                    new_ext->next = cur_ext;
                    prev_ext->next = new_ext;
                }
                if (obj.attr & AFS_ATTR_DIR) {
                    int cstat = check_walk(ctx, &obj, dir, ent_path, ent_len);
                    if (status == AFS_OK)
                        status = cstat;
                }
                prev = ent;
            }
        }
        else
            fprintf(ctx->mfp, "%s:%s: broken direcrory: Hugo/sequence\n", ctx->fsname, path);
    }
    else
        fprintf(ctx->mfp, "%s:%s: unable to load directory: %s\n", ctx->fsname, path, acorn_fs_strerr(status));
    return status;
}

static const char name_free[] = "(free)";

static void free_ext(extent *ext)
{
    if (ext->name != name_free)
        free(ext->name);
    free(ext);
}

static int adfs_check(acorn_fs *fs, const char *fsname, FILE *mfp)
{
    int status = load_fsmap(fs);
    if (status == AFS_OK) {
        unsigned char *fsmap = fs->priv;
        unsigned char *sizes = fsmap + 0x100;
        int end = fsmap[0x1fe];
        if (end == 0) {
            fprintf(mfp, "%s: free space map empty\n", fsname);
            status = AFS_BAD_FSMAP;
        }
        else {
            extent *tail = malloc(sizeof(extent));
            if (tail) {
                check_ctx ctx;
                ctx.head = tail;
                unsigned cur_posn = adfs_get24(fsmap);
                unsigned cur_size = adfs_get24(sizes);
                tail->posn = cur_posn;
                tail->size = cur_size;
                tail->next = NULL;
                tail->name = (char *)name_free;
                for (int ent = 3; ent < end; ent += 3) {
                    unsigned new_posn = adfs_get24(fsmap + ent);
                    unsigned new_size = adfs_get24(sizes + ent);
                    if (new_posn < cur_posn) {
                        fprintf(mfp, "%s: free space map out of order at entry %d\n", fsname, ent);
                        status = AFS_BAD_FSMAP;
                        break;
                    }
                    if (cur_posn + cur_size > new_posn) {
                        fprintf(mfp, "%s: free space map overlap at entry %d\n", fsname, ent);
                        status = AFS_BAD_FSMAP;
                        break;
                    }
                    extent *ent = malloc(sizeof(extent));
                    if (!ent) {
                        status = errno;
                        break;
                    }
                    ent->posn = new_posn;
                    ent->size = new_size;
                    ent->next = NULL;
                    ent->name = (char *)name_free;
                    tail->next = ent;
                    tail = ent;
                    cur_posn = new_posn;
                    cur_size = new_size;
                }
                if (status == AFS_OK) {
                    acorn_fs_object root;
                    make_root(&root);
                    ctx.fs = fs;
                    ctx.fsname = fsname;
                    ctx.tail = tail;
                    ctx.mfp = mfp;
                    status = check_walk(&ctx, &root, &root, root.name, 1);
                    if (status == AFS_OK) {
                        extent *cur = ctx.head;
                        extent *next = cur->next;
                        while (next) {
                            int delta = cur->posn + cur->size - next->posn;
                            if (delta) {
                                const char *which = delta < 0 ? "gap" : "overlap";
                                fprintf(mfp, "%s: free/used space inconsistency: %s between %s and %s\n", fsname, which, cur->name, next->name);
                                status = AFS_CORRUPT;
                            }
                            free_ext(cur);
                            cur = next;
                            next = cur->next;
                        }
                        free_ext(cur);
                    }
                }
            }
            else
                status = errno;
        }
    }
    return status;
}


static int adfs_mkdir(acorn_fs *fs, const char *name, acorn_fs_object *dest)
{
    int status;
    acorn_fs_object child;
    unsigned char *ent;

    // Create empty directory data
    unsigned char empty_data[1280];
    memset(empty_data, 0, sizeof(empty_data));
    strcpy((char *)empty_data + 0x001, "Hugo");
    strcpy((char *)empty_data + 0x4FB, "Hugo");

    // Create an empty directory acorn_fs_object
    acorn_fs_object empty_obj;
    memset(&empty_obj, 0, sizeof(empty_obj));
    strncpy(empty_obj.name, name, ACORN_FS_MAX_NAME);
    empty_obj.load_addr = 0;
    empty_obj.exec_addr = 0;
    empty_obj.length = 1280;
    empty_obj.attr = AFS_ATTR_DIR | AFS_ATTR_LOCKED | AFS_ATTR_UREAD;
    empty_obj.data = empty_data;

    // Save the new directory
    //
    // I wanted to re-use adfs_save here, but mkdir needs to fail if an object
    // with the same name already exists, where-as adfs_save will overwrite it
    if (!(dest->attr & AFS_ATTR_DIR)) {
        status = ENOTDIR;
    } else if ((status = load_fsmap(fs)) == AFS_OK) {
        if ((status = search(fs, dest, &child, name, &ent)) == AFS_OK) {
            status = EEXIST;
        } else if (status == ENOENT) {
            if (ent == NULL)
                status = AFS_DIR_FULL;
            else if ((status = alloc_write(fs, &empty_obj)) == AFS_OK) {
                dir_makeslot(dest, ent);
                status = dir_update(fs, dest, &empty_obj, ent);
            }
        }
        if (status == AFS_OK) {
            status = save_fsmap(fs);
        }
    }
    return status;
}

void acorn_fs_adfs_init(acorn_fs *fs)
{
    fs->find = adfs_find;
    fs->glob = adfs_glob;
    fs->walk = adfs_walk;
    fs->load = adfs_load;
    fs->mkdir = adfs_mkdir;
    fs->save = adfs_save;
    fs->check = adfs_check;
    fs->priv = NULL;
    fs->settitle = adfs_settitle;
}
