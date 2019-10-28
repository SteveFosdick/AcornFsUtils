#include "acorn-adfs.h"
#include <stdlib.h>
#include <string.h>

#define FSMAP_MAX_ENT 82
#define FSMAP_SIZE    0x200
#define DIR_HDR_SIZE  0x05
#define DIR_ENT_SIZE  0x1A
#define DIR_FTR_SIZE  0x35

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
    obj->is_dir = 1;
    obj->length = 1280;
    obj->sector = 2;
}

int acorn_adfs_load(acorn_fs *fs, acorn_fs_object *obj)
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
    obj->name[i]    = '\0';
    obj->user_read  = ((ent[0] & 0x80) == 0x80);
    obj->user_write = ((ent[1] & 0x80) == 0x80);
    obj->locked     = ((ent[2] & 0x80) == 0x80);
    obj->is_dir     = ((ent[3] & 0x80) == 0x80);
    obj->user_exec  = ((ent[4] & 0x80) == 0x80);
    obj->pub_read   = ((ent[5] & 0x80) == 0x80);
    obj->pub_write  = ((ent[6] & 0x80) == 0x80);
    obj->pub_exec   = ((ent[7] & 0x80) == 0x80);
    obj->pub_exec   = ((ent[8] & 0x80) == 0x80);
    obj->priv       = ((ent[9] & 0x80) == 0x80);
    obj->load_addr  = adfs_get32(ent + 0x0a);
    obj->exec_addr  = adfs_get32(ent + 0x0e);
    obj->length     = adfs_get32(ent + 0x12);
    obj->sector     = adfs_get24(ent + 0x16);
    return i;
}

static int search(acorn_fs *fs, acorn_fs_object *parent, acorn_fs_object *child, const char *name, int name_len, unsigned char **ent_ptr)
{
    if (name_len > ADFS_MAX_NAME)
        return ENAMETOOLONG;
    if (!parent->is_dir)
        return ENOTDIR;
    int status = acorn_adfs_load(fs, parent);
    if (status == AFS_OK) {
        if ((status = check_dir(parent)) == AFS_OK) {
            unsigned char *ent = parent->data;
            unsigned char *end = ent + parent->length;
            for (ent += DIR_HDR_SIZE; ent < end; ent += DIR_ENT_SIZE) {
                if (*ent == 0) {
                    *ent_ptr = ent;
                    return ENOENT;
                }
                bool is_dir = ent[3] & 0x80;
                int i = acorn_fs_wildmat(name, ent, name_len, is_dir);
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


int acorn_adfs_find(acorn_fs *fs, const char *adfs_name, acorn_fs_object *obj)
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
        if ((status = search(fs, parent, child, adfs_name, ptr - adfs_name, &ent)) != AFS_OK) {
            acorn_fs_free_obj(parent);
            return status;
        }
        temp = parent;
        parent = child;
        child = temp;
        adfs_name = ptr + 1;
    }
    status = search(fs, parent, obj, adfs_name, strlen(adfs_name), &ent);
    acorn_fs_free_obj(parent);
    return status;
}

static int glob_dir(acorn_fs *fs, acorn_fs_object *dir, const char *pattern, acorn_fs_cb cb, void *udata, char *path, unsigned path_posn)
{
    if (!*pattern)
        return AFS_OK;
    int status = acorn_adfs_load(fs, dir);
    if (status == AFS_OK) {
        if ((status = check_dir(dir)) == AFS_OK) {
            unsigned char *ent = dir->data;
            unsigned char *end = ent + dir->length - DIR_FTR_SIZE;
            char *sep = strchr(pattern, '.');
            unsigned mat_len = sep ? sep - pattern + 1 : strlen(pattern);
            for (ent += DIR_HDR_SIZE; ent < end; ent += DIR_ENT_SIZE) {
                if (!*ent)
                    break;
                bool is_dir = ent[3] & 0x80;
                int i = acorn_fs_wildmat(pattern, ent, mat_len, is_dir);
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
                    if (obj.is_dir && sep) {
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

int acorn_adfs_glob(acorn_fs *fs, acorn_fs_object *start, const char *pattern, acorn_fs_cb cb, void *udata)
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
    int status = acorn_adfs_load(fs, dir);
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
                if (obj.is_dir) {
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

int acorn_adfs_walk(acorn_fs *fs, acorn_fs_object *start, acorn_fs_cb cb, void *udata)
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
            if (end >= 82)
                return AFS_MAP_FULL;
            bytes = (end - ent) * 3;
            memmove(fsmap + ent + 3, fsmap + ent, bytes);
            memmove(sizes + ent + 3, sizes + ent, bytes);
            break;
        }
    }
    if (end >= 82)
        return AFS_MAP_FULL;
    adfs_put24(fsmap + ent, obj->sector);
    adfs_put24(sizes + ent, obj_size);
    fsmap[0x1fe]++;
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
                bytes = (end - ent) * 3;
                memmove(fsmap + ent, fsmap + ent + 3, bytes);
                memmove(sizes + ent, sizes + ent + 3, bytes);
                fsmap[0x1fe]--;
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
    int i, ch;

    for (i = 0; i < ADFS_MAX_NAME; i++) {
        ch = child->name[i];
        ent[i] = ch & 0x7f;
        if (ch == 0)
            break;
    }
    if (child->user_read)  ent[0] |= 0x80;
    if (child->user_write) ent[1] |= 0x80;
    if (child->locked)     ent[2] |= 0x80;
    if (child->is_dir)     ent[3] |= 0x80;
    if (child->user_exec)  ent[4] |= 0x80;
    if (child->pub_read)   ent[5] |= 0x80;
    if (child->pub_write)  ent[6] |= 0x80;
    if (child->pub_exec)   ent[7] |= 0x80;
    if (child->pub_exec)   ent[8] |= 0x80;
    if (child->priv)       ent[9] |= 0x80;
    adfs_put24(ent + 0x0a, child->load_addr);
    adfs_put24(ent + 0x0e, child->exec_addr);
    adfs_put24(ent + 0x12, child->length);
    adfs_put24(ent + 0x16, child->sector);
    return fs->wrsect(fs, parent->sector, parent->data, parent->length);
}

static void dir_makeslot(acorn_fs_object *parent, unsigned char *ent)
{
    unsigned char *ftr = parent->data + parent->length - DIR_FTR_SIZE;
    unsigned bytes = ftr - ent - DIR_ENT_SIZE;
    memmove(ent + DIR_ENT_SIZE, ent, bytes);
}

int acorn_adfs_save(acorn_fs *fs, acorn_fs_object *obj, acorn_fs_object *dest)
{
    int status;
    acorn_fs_object parent, child;
    unsigned char *ent;

    if (!dest->is_dir)
        status = ENOTDIR;
    else if ((status = load_fsmap(fs)) == AFS_OK) {
        if ((status = search(fs, dest, &child, obj->name, strlen(obj->name), &ent)) == AFS_OK) {
            if ((status = map_free(fs, &child)) == AFS_OK)
                if ((status = alloc_write(fs, obj)) == AFS_OK)
                    status = dir_update(fs, &parent, obj, ent);
        }
        else if (status == ENOENT) {
            if (ent == NULL)
                status = AFS_DIR_FULL;
            else {
                if ((status = alloc_write(fs, obj)) == AFS_OK) {
                    dir_makeslot(&parent, ent);
                    status = dir_update(fs, &parent, obj, ent);
                }
            }
        }
        if (status == AFS_OK)
            status = save_fsmap(fs);
    }
    return status;
}


