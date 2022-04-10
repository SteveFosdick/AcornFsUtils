#include "acorn-fs.h"
#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

static int dfs_wildmat2(const char *pattern, const unsigned char *candidate, size_t len)
{
    while (len) {
        int pat_ch = *(const unsigned char *)pattern++;
        if (pat_ch == '*') {
            pat_ch = *pattern;
            if (!pat_ch)
                return 0;
            while (len)
                if (!dfs_wildmat2(pattern, candidate++, len--))
                    return 0;
            return 1;
        }
        else {
            int can_ch = *candidate++ & 0x5f;
            if (!pat_ch)
                return can_ch;
            if (pat_ch != '#') {
                pat_ch &= 0x5f;
                int d = pat_ch - can_ch;
                if (d)
                    return d;
            }
            len--;
        }
    }
    return *pattern;
}

static int dfs_wildmat(const char *pattern, const unsigned char *candidate)
{
    int pat_ch0 = pattern[0];
    if (pat_ch0) {
        int pat_ch1 = pattern[1];
        if (pat_ch1 == '.') {
            if (pat_ch0 == '*' || pat_ch1 == '#' || pat_ch0 == (candidate[7] & 0x7f))
                return dfs_wildmat2(pattern, candidate, 7);
        }
        else if ((candidate[7] & 0x7f) == '$')
            return dfs_wildmat2(pattern, candidate, 7);
    }
    return ENOENT;
}

static void ent2obj(const unsigned char *ent, acorn_fs_object *obj)
{
    obj->name[0] = ent[7] & 0x7f;
    obj->name[1] = '.';
    int i;
    for (i = 0; i < 7; i++) {
        int ch = ent[i];
        if (!ch || ch == ' ')
            break;
        obj->name[i] = ch;
    }
    obj->name[i] = 0;
    unsigned attr = AFS_ATTR_UREAD|AFS_ATTR_UWRITE;
    if (ent[7] & 0x80)
        attr |= AFS_ATTR_LOCKED;
    obj->attr = attr;
    unsigned b6 = ent[0x106];
    unsigned ub = b6 & 0x0c;
    obj->load_addr = ent[0x100] | (ent[0x101] << 8) | ((ub == 0x0c) ? 0xffff0000 : (ub << 14));
    ub = b6 & 0xc0;
    obj->exec_addr = ent[0x102] | (ent[0x103] << 8) | ((ub == 0xc0) ? 0xffff0000 : (ub << 10));
    obj->length = ent[0x104] | (ent[0x105] << 8) | ((b6 & 0x30) << 12);
    obj->sector = ((b6 & 0x03) << 8) | ent[0x107];
    obj->data = NULL;
}

static int dfs_find(acorn_fs *fs, const char *dfs_name, acorn_fs_object *obj)
{
    unsigned char *dir = fs->priv;
    unsigned char *ent = dir + 8;
    unsigned char *end = ent + dir[0x105];
    while (ent < end) {
        if (!dfs_wildmat(dfs_name, ent)) {
            ent2obj(ent, obj);
            return AFS_OK;
        }
        ent += 8;
    }
    return ENOENT;
}

static int dfs_settitle(acorn_fs *fs, const char *title)
{
    // In DFS the title is 12 character, written to:
    // Sector 0, bytes 0..7
    // Sector 1, bytes 0..3
    // Padding is typically spaces
    unsigned char *dir = fs->priv;
    for (int i = 0; i < 12; i++) {
        char c = (i < strlen(title)) ? title[i] : ' ';
        int di = (i > 8) ? 0x100 + i - 8 : i;
        // Update the directory
        dir[di] = c;
    }
    return fs->wrsect(fs, 0, dir, 0x200);
}

static int dfs_glob(acorn_fs *fs, acorn_fs_object *start, const char *pattern, acorn_fs_cb cb, void *udata)
{
    unsigned char *dir = fs->priv;
    unsigned char *ent = dir + 8;
    unsigned char *end = ent + dir[0x105];
    while (ent < end) {
        if (!dfs_wildmat(pattern, ent)) {
            acorn_fs_object obj;
            ent2obj(ent, &obj);
            int status = cb(fs, &obj, udata, obj.name);
            if (status != AFS_OK)
                return status;
        }
        ent += 8;
    }
    return AFS_OK;
}

static int dfs_walk(acorn_fs *fs, acorn_fs_object *start, acorn_fs_cb cb, void *udata)
{
    unsigned char *dir = fs->priv;
    unsigned char *ent = dir + 8;
    unsigned char *end = ent + dir[0x105];
    while (ent < end) {
        acorn_fs_object obj;
        ent2obj(ent, &obj);
        int status = cb(fs, &obj, udata, obj.name);
        if (status != AFS_OK)
            return status;
        ent += 8;
    }
    return AFS_OK;
}

static int dfs_remove(acorn_fs *fs, acorn_fs_object *start, const char *pattern)
{
    unsigned char *dir = fs->priv;
    unsigned char *ent = dir + 8;
    unsigned char *end = ent + dir[0x105];
    bool dirty = false;
    while (ent < end) {
        if (!dfs_wildmat(pattern, ent)) {
			/* removing, close the gap */
			size_t bytes = (end - ent) - 8;
			memmove(ent, ent+8, bytes); // names.
			ent[bytes] = 0;
			memmove(ent+0x100, ent+0x108, bytes); // other details.
			/* adjust the total */
			dir[0x105] -= 8;
			dirty = true;
		}
		else
			ent += 8;
    }
    return dirty ? fs->wrsect(fs, 0, dir, 0x200) : AFS_OK;
}

static int dfs_load(acorn_fs *fs, acorn_fs_object *obj)
{
    unsigned char *data = malloc(obj->length);
    if (data) {
        obj->data = data;
        return fs->rdsect(fs, obj->sector, data, obj->length);
    }
    return errno;
}

static unsigned sectors(unsigned bytes)
{
    if (!bytes)
        return 0;
    return (bytes - 1) / ACORN_FS_SECT_SIZE + 1;
}

static void obj2ent(acorn_fs_object *obj, char *name, int dfs_dir, unsigned ssect, unsigned char *ent)
{
    if (obj->attr & AFS_ATTR_LOCKED)
        dfs_dir |= 0x80;
    ent[7] = dfs_dir;
    int i;
    for (i = 0; i < 7; i++) {
        int ch = name[i];
        if (!ch)
            break;
        ent[i] = ch;
    }
    while (i < 7)
        ent[i++] = ' ';
    ent[0x100] = obj->load_addr;
    ent[0x101] = obj->load_addr >> 8;
    ent[0x102] = obj->exec_addr;
    ent[0x103] = obj->exec_addr >> 8;
    ent[0x104] = obj->length;
    ent[0x105] = obj->length >> 8;
    ent[0x106] = ((obj->exec_addr & 0x00030000) >> 10) | ((obj->length & 0x00030000) >> 12) | ((obj->load_addr & 0x00030000) >> 14) | ((ssect & 0x00000300) >> 8);
    ent[0x107] = ssect;
}

static int dfs_save(acorn_fs *fs, acorn_fs_object *obj, acorn_fs_object *dest)
{
    int dfs_dir = '$';
    char *name = obj->name;
    int pat_ch0 = name[0];
    if (!pat_ch0)
        return EINVAL;
    if (obj->name[1] == '.') {
        dfs_dir = pat_ch0;
        name += 2;
    }
    unsigned char *dir = fs->priv;
    unsigned char *fde = dir + 8;
    unsigned char *end = fde + dir[0x105];
    unsigned char *name_ent = fde;
    bool found = false;
    while (name_ent < end) {
        if (dfs_dir == (name_ent[7] & 0x7f)) {
            found = true;
            for (int i = 0; i < 7; i++) {
                int pat_ch = name[i];
                int ent_ch = name_ent[i];
                if (!pat_ch && ent_ch == ' ')
                    break;
                if ((pat_ch & 0x5f) != (ent_ch & 0x5f)) {
                    found = false;
                    break;
                }
            }
        }
        if (found)
            break;
        name_ent += 8;
    }
    unsigned reqd_sect = sectors(obj->length);
    unsigned start_sect = 2;
    unsigned char *space_ent = NULL;
    if (found) {
        // Existing dir entry found - is the space allocation big enough?
        unsigned cur_len = ((name_ent[0x106] & 0x30) << 4) | name_ent[0x104] | (name_ent[0x105] << 8);
        if (reqd_sect <= sectors(cur_len)) {
            // It fits.
            space_ent = name_ent;
            start_sect = ((name_ent[0x106] & 0x03) << 8) | name_ent[0x107];
        }
    }
    else if (end >= (dir + 0x100))
        return AFS_DIR_FULL;
    if (!space_ent) {
        // Work backwards looking for a suitable space.
        space_ent = end;
        while (space_ent > fde) {
            space_ent -= 8;
            unsigned this_start = ((space_ent[0x106] & 0x03) << 8) | space_ent[0x107];
            if ((this_start - start_sect) >= reqd_sect) {
                space_ent += 8;
                break;
            }
            unsigned this_len = ((space_ent[0x106] & 0x30) << 4) | space_ent[0x104] | (space_ent[0x105] << 8);
            start_sect = this_start + sectors(this_len);
        }
    }
    unsigned avail_sect = (((dir[0x106] & 0x03) << 8) | dir[0x107]) - 2;
    if (reqd_sect > (avail_sect - start_sect))
        return ENOSPC;
    int status = fs->wrsect(fs, start_sect, obj->data, obj->length);
    if (status == AFS_OK) {
        if (space_ent != name_ent) {
            if (name_ent > space_ent) {
                unsigned bytes = name_ent - space_ent;
                memmove(space_ent + 8, space_ent, bytes);
                memmove(space_ent + 0x108, space_ent + 0x100, bytes);
            }
            else {
                space_ent -= 8;
                unsigned bytes = space_ent - name_ent;
                memmove(name_ent, name_ent + 8, bytes);
                memmove(name_ent + 0x100, name_ent + 0x108, bytes);
            }
        }
        obj2ent(obj, name, dfs_dir, start_sect, space_ent);
        if (!found)
            dir[0x105] += 8;
        status = fs->wrsect(fs, 0, dir, 0x200);
    }
    return status;
}

int acorn_fs_dfs_check(acorn_fs *fs, const char *fsname, FILE *mfp)
{
    unsigned char *dir = fs->priv;
    unsigned dirsize = dir[0x105];
    if ((dirsize & 0x07) || dirsize > (31 * 8)) {
        if (mfp)
            fprintf(mfp, "%s: invalid directory used count %d\n", fsname, dirsize);
        return AFS_CORRUPT;
    }
    unsigned sects = ((dir[0x106] & 0x07) << 8) | dir[0x107];
    if (sects > 1280) {
        if (mfp)
            fprintf(mfp, "%s: implausible total sector count %d\n", fsname, sects);
        return AFS_CORRUPT;
    }
    // Check the files are sorted by decreasing start sector.
    unsigned char *ent = dir;
    unsigned cur_start = UINT_MAX;
    while (dirsize > 0) {
        ent += 8;
        dirsize -= 8;
        unsigned new_start = ((ent[0x106] & 0x03) << 8) | ent[0x107];
        if (new_start == 0) {
            if (mfp)
                fprintf(mfp, "%s: impossible start sector (zero)\n", fsname);
            return AFS_CORRUPT;
        }
        if (new_start > cur_start) {
            if (mfp)
                fprintf(stderr, "%s: catalogue not sorted\n", fsname);
            return AFS_CORRUPT;
        }
        cur_start = new_start;
    }
    return AFS_OK;
}

static int dfs_mkdir(acorn_fs *fs, acorn_fs_object *obj, acorn_fs_object *dest)
{
    return ENOSYS;
}

void acorn_fs_dfs_init(acorn_fs *fs)
{
    fs->find  = dfs_find;
    fs->glob  = dfs_glob;
    fs->walk  = dfs_walk;
    fs->remove = dfs_remove;
    fs->load  = dfs_load;
    fs->mkdir = dfs_mkdir;
    fs->save  = dfs_save;
    fs->check = acorn_fs_dfs_check;
    fs->settitle = dfs_settitle;
}
