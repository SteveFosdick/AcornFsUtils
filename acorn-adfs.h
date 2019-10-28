#ifndef ACORN_ADFS_INC
#define ACORN_ADFS_INC

#define ADFS_MAX_NAME 10

extern int acorn_adfs_find(acorn_fs *fs, const char *adfs_name, acorn_fs_object *obj);
extern int acorn_adfs_glob(acorn_fs *fs, acorn_fs_object *start, const char *pattern, acorn_fs_cb cb, void *udata);
extern int acorn_adfs_walk(acorn_fs *fs, acorn_fs_object *start, acorn_fs_cb cb, void *udata);
extern int acorn_adfs_load(acorn_fs *fs, acorn_fs_object *obj);
extern int acorn_adfs_save(acorn_fs *fs, acorn_fs_object *obj, acorn_fs_object *dest);

#endif
