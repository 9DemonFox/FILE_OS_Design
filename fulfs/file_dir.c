#include "file_dir.h"

#include "base_file.h"
#include "../utils/math.h"
#include "../utils/log.h"
#include <string.h>
#include <assert.h>

#define FILE_MAX_NAME 14

static bool dir_locate(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name, bool* p_exist, inode_no_t* p_no);
static bool dir_add(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name, inode_no_t no);
static bool dir_del(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name);
static bool dir_tree_locate(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t start, const char* relative_path, bool* p_exist, inode_no_t* p_no);

static void dir_name(const char* path, char* dir);
static void base_name(const char* path, char* name);

bool fulfs_open(fulfs_file_t* file, device_handle_t device, fulfs_filesystem_t* fs, const char* path)
{
    if (path[0] != '/') {
        log_warning("fulfs_open函数不支持相对路径: %s\n", path);
        return false;
    }

    char dir_path[FS_MAX_FILE_PATH];
    char name[FILE_MAX_NAME];
    dir_name(path, dir_path);
    base_name(path, name);

    /* 定位出目录所在的inode */
    bool exist;
    inode_no_t dir_no;
    bool success = dir_tree_locate(device, fs, superblock_root_dir_inode(&fs->sb), dir_path + 1, &exist, &dir_no);
    if (!success) {
        return false;
    }

    inode_no_t file_no;
    if (!exist) {
        bool success = base_file_create(device, &fs->sb, MODE_FILE, &file_no);
        if (!success) {
            return false;
        }

        success =  dir_add(device, fs, dir_no, name, file_no);
        /* FIXME: 这里失败了应该要把新建立的文件删除 */
        if (!success) {
            return false;
        }
    } else {
        bool exist;
        bool success = dir_locate(device, fs, dir_no, name, &exist, &file_no);
        if (!success) {
            return false;
        }

        assert(exist);
    }

    success = base_file_open(&file->base_file, device, &fs->sb, file_no);
    if (!success) {
        return false;
    }

    return true;
}

void fulfs_close(fulfs_file_t* file)
{
    base_file_close(&file->base_file);
}

int fulfs_read(fulfs_file_t* file, char* buf, int count)
{
    return base_file_read(&file->base_file, buf, count);
}

int fulfs_write(fulfs_file_t* file, const char* buf, int count)
{
    return base_file_write(&file->base_file, buf, count);
}

bool fulfs_ftruncate(fulfs_file_t* file, fsize_t size)
{
    return base_file_truncate(&file->base_file, size);
}

fs_off_t fulfs_lseek(fulfs_file_t* file, fs_off_t off, int where)
{
    fsize_t new_off;
    if (where == FS_SEEK_SET) {
        new_off = off;
    } else if (where == FS_SEEK_CUR) {
        new_off = base_file_tell(&file->base_file) + off;
    } else if (where == FS_SEEK_END) {
        new_off = base_file_size(&file->base_file) + off;
    } else {
        return FS_ERROR;
    }

    if (new_off > base_file_size(&file->base_file)) {
        return FS_ERROR;
    }

    base_file_seek(&file->base_file, off);

    return new_off;
}

bool fulfs_mkdir(device_handle_t device, fulfs_filesystem_t* fs, const char* path)
{
    if (path[0] != '/') {
        log_warning("fulfs_mkdir函数不支持相对路径: %s\n", path);
        return false;
    }

    char dir_path[FS_MAX_FILE_PATH];
    char name[FILE_MAX_NAME];
    dir_name(path, dir_path);
    base_name(path, name);

    /* 定位出目录所在的inode */
    bool exist;
    inode_no_t dir_no;
    bool success = dir_tree_locate(device, fs, superblock_root_dir_inode(&fs->sb), dir_path + 1, &exist, &dir_no);
    if (!success) {
        return false;
    }

    inode_no_t file_no;
    if (!exist) {
        bool success = base_file_create(device, &fs->sb, MODE_DIR, &file_no);
        if (!success) {
            return false;
        }

        success =  dir_add(device, fs, dir_no, name, file_no);
        /* FIXME: 这里失败了应该要把新建立的文件删除 */
        if (!success) {
            return false;
        }
    }

    return true;
}

bool fulfs_rmdir(device_handle_t device, fulfs_filesystem_t* fs, const char* path)
{
    if (path[0] != '/') {
        log_warning("fulfs_rmdir函数不支持相对路径: %s\n", path);
        return false;
    }

    bool exist;
    inode_no_t dir_no;
    bool success = dir_tree_locate(device, fs, superblock_root_dir_inode(&fs->sb), path + 1, &exist, &dir_no);
    if (!success) {
        return false;
    }

    base_file_t base_file;
    success = base_file_open(&base_file, device, &fs->sb, dir_no);
    if (!success) {
        return false;
    }

    bool is_dir = (base_file_mode(&base_file) == MODE_DIR);
    base_file_close(&base_file);

    if (is_dir) {
        base_file_unref(device, &fs->sb, dir_no);
        return true;
    } else {
        return false;
    }
}

bool fulfs_link(device_handle_t device, fulfs_filesystem_t* fs, const char* src_path, const char* new_path)
{

    if (src_path[0] != '/' || new_path[0] != '/') {
        log_warning("fulfs_link函数不支持相对路径: %s, %s\n", src_path, new_path);
        return false;
    }


    /* src文件所在的inode */
    bool exist;
    inode_no_t no;
    bool success = dir_tree_locate(device, fs, superblock_root_dir_inode(&fs->sb), src_path + 1, &exist, &no);
    if (!success || !exist) {
        return false;
    }


    /* 检查是满足硬链接要求 */
    base_file_t base_file;
    success = base_file_open(&base_file, device, &fs->sb, no);
    if (!success) {
        return false;
    }
    if (base_file_mode(&base_file) != MODE_FILE) {
        log_warning("不能给除真正文件之外的东西做硬链接: %s, %s\n", src_path, new_path);
        return false;
    }
    base_file_close(&base_file);


    /* 引用计数加1 */
    success = base_file_ref(device, &fs->sb, no);
    if (!success) {
        return false;
    }


    /* 创建硬链接 */
    char dir_path[FS_MAX_FILE_PATH];
    char name[FILE_MAX_NAME];
    dir_name(src_path, dir_path);
    base_name(src_path, name);

    inode_no_t dir_no;
    success = dir_tree_locate(device, fs, superblock_root_dir_inode(&fs->sb), dir_path + 1, &exist, &dir_no);
    if (!success || !exist) {
        /* 这个要是失败了，很尴尬的事情 */
        base_file_unref(device, &fs->sb, no);
        return false;
    }

    success = dir_add(device, fs, dir_no, name, no);
    if (!success) {
        /* 这个要是失败了，很尴尬的事情 */
        base_file_unref(device, &fs->sb, no);
        return false;
    }

    return true;
}

bool fulfs_unlink(device_handle_t device, fulfs_filesystem_t* fs, const char* path)
{
    if (path[0] != '/') {
        log_warning("fulfs_unlink函数不支持相对路径: %s\n", path);
        return false;
    }

    char dir_path[FS_MAX_FILE_PATH];
    char name[FILE_MAX_NAME];
    dir_name(path, dir_path);
    base_name(path, name);

    bool exist;
    inode_no_t dir_no;
    bool success = dir_tree_locate(device, fs, superblock_root_dir_inode(&fs->sb), dir_path + 1, &exist, &dir_no);
    if (!success || !exist) {
        return false;
    }

    inode_no_t file_no;
    success = dir_locate(device, fs, dir_no, name, &exist, &file_no);
    if (!success || !exist) {
        return false;
    }

    /* 检查是满足要求 */
    base_file_t base_file;
    success = base_file_open(&base_file, device, &fs->sb, file_no);
    if (!success) {
        return false;
    }
    if (base_file_mode(&base_file) == MODE_DIR) {
        return false;
    }
    base_file_close(&base_file);


    success = dir_del(device, fs, dir_no, name);
    if (!success) {
        return false;
    }

    success = base_file_unref(device, &fs->sb, file_no);
    if (!success) {
        return false;
    }

    return true;
}

bool fulfs_symlink(device_handle_t device, fulfs_filesystem_t* fs, const char* src_path, const char* new_path)
{
    if (src_path[0] != '/') {
        log_warning("fulfs_symlink函数不支持相对路径: %s\n", src_path);
        return false;
    }


    return false;
}

bool fulfs_readlink(device_handle_t device, fulfs_filesystem_t* fs, const char *path, char *buf, size_t size)
{
    /* TODO */
    return false;
}


/*************************************/
#define DIR_ITEM_NAME_SIZE 14
#define DIR_ITEM_SIZE (DIR_ITEM_NAME_SIZE + sizeof(inode_no_t))

struct dir_item_s {
    char name[DIR_ITEM_NAME_SIZE + 1];
    block_no_t inode_no;
};

static void dir_item_load_from_bin(struct dir_item_s* item, const char* bin);
static void dir_item_dump_to_bin(const struct dir_item_s* item, char* bin);

static bool dir_locate(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name, bool* p_exist, inode_no_t* p_no)
{
    base_file_t base_file;
    bool success = base_file_open(&base_file, device, &fs->sb, dir);
    if (!success) {
        return false;
    }

    assert(base_file_size(&base_file) % DIR_ITEM_SIZE == 0);

    char buf[DIR_ITEM_SIZE];
    while (base_file_tell(&base_file) < base_file_size(&base_file)) {
        int count = base_file_read(&base_file, buf, DIR_ITEM_SIZE);
        if (count != DIR_ITEM_SIZE) {
            base_file_close(&base_file);
            return false;
        }

        struct dir_item_s dir_item;
        dir_item_load_from_bin(&dir_item, buf);
        if (strcmp(dir_item.name, name) == 0) {
            *p_exist = true;
            *p_no = dir_item.inode_no;
            base_file_close(&base_file);
            return true;
        }
    }

    base_file_close(&base_file);
    *p_exist = false;
    return true;
}

static bool dir_add(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name, inode_no_t no)
{
    base_file_t base_file;
    bool success = base_file_open(&base_file, device, &fs->sb, dir);
    if (!success) {
        return false;
    }

    assert(base_file_size(&base_file) % DIR_ITEM_SIZE == 0);

    char buf[DIR_ITEM_SIZE];
    struct dir_item_s dir_item;
    strncpy(dir_item.name, name, DIR_ITEM_NAME_SIZE);
    dir_item.name[DIR_ITEM_NAME_SIZE] = '\0';
    dir_item_dump_to_bin(&dir_item, buf);
    dir_item.inode_no = no;

    base_file_seek(&base_file, base_file_size(&base_file));

    /* FIXME: 这里写入不完整的话，应该把文件截断到之前的状态 */
    int count = base_file_write(&base_file, buf, DIR_ITEM_SIZE);
    if (count != DIR_ITEM_SIZE) {
        base_file_close(&base_file);
        return false;
    }

    base_file_close(&base_file);
    return true;
}

static bool dir_del(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name)
{
    base_file_t base_file;
    bool success = base_file_open(&base_file, device, &fs->sb, dir);
    if (!success) {
        return false;
    }

    assert(base_file_size(&base_file) % DIR_ITEM_SIZE == 0);

    char buf[DIR_ITEM_SIZE];
    while (base_file_tell(&base_file) < base_file_size(&base_file)) {
        int count = base_file_read(&base_file, buf, DIR_ITEM_SIZE);
        if (count != DIR_ITEM_SIZE) {
            base_file_close(&base_file);
            return false;
        }

        struct dir_item_s dir_item;
        dir_item_load_from_bin(&dir_item, buf);
        if (strcmp(dir_item.name, name) == 0) {
            /* 把最后一项读出来覆盖欲删除的项 */
            fsize_t current = base_file_tell(&base_file) - DIR_ITEM_SIZE;

            base_file_seek(&base_file, base_file_size(&base_file) - DIR_ITEM_SIZE);
            int count = base_file_read(&base_file, buf, DIR_ITEM_SIZE);
            if (count != DIR_ITEM_SIZE) {
                base_file_close(&base_file);
                return false;
            }

            base_file_seek(&base_file, current);
            count = base_file_write(&base_file, buf, DIR_ITEM_SIZE);
            if (count != DIR_ITEM_SIZE) {
                base_file_close(&base_file);
                return false;
            }

            /* FIXME: 这里要是失败了，又是个很尴尬的问题，会导致这个目录里出现两个相同的项 */
            bool success = base_file_truncate(&base_file, base_file_size(&base_file) - DIR_ITEM_SIZE);
            if (!success) {
                base_file_close(&base_file);
                return false;
            }

            base_file_close(&base_file);
            return true;
        }
    }

    base_file_close(&base_file);
    return true;
}



/*************************************/

/* 简单实现，考虑了内存布局但是未考虑字节序 */
static void dir_item_load_from_bin(struct dir_item_s* item, const char* bin)
{
    /* 二进制的数据里面，文件名称在达到14个字节的情况下，允许不带\0 */
    memcpy(&item->inode_no, bin, sizeof(inode_no_t));
    memcpy(item->name, bin  + sizeof(inode_no_t), DIR_ITEM_NAME_SIZE);
    item->name[DIR_ITEM_NAME_SIZE] = '\0';
}

static void dir_item_dump_to_bin(const struct dir_item_s* item, char* bin)
{
    memcpy(bin, &item->inode_no, sizeof(inode_no_t));
    memcpy(bin  + sizeof(inode_no_t), item->name, DIR_ITEM_NAME_SIZE);
}

static bool dir_tree_locate(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t start, const char* relative_path, bool* p_exist, inode_no_t* p_no)
{
    assert(relative_path[0] != '/');

    char name[DIR_ITEM_NAME_SIZE + 1] = {'\0'};

    int count = 0;
    for (const char* p = relative_path; *p != '\0'; p++) {
        if (*p == '/') {
            count = 0;

            bool exist;
            inode_no_t no;
            strncpy(name, p - count, min_int(DIR_ITEM_NAME_SIZE, count));
            bool success = dir_locate(device, fs, start, name, &exist, &no);
            if (!success) {
                return false;
            }

            if (!exist) {
                *p_exist = false;
                return false;
            } else {
                start = no;
            }

        } else {
            count++;
        }
    }

    *p_exist = true;
    *p_no = start;
    return true;
}

static void dir_name(const char* path, char* dir)
{
    strcpy(dir, path);
    int size = strlen(path);

    bool is_abs = (dir[0] == '/');

    for (int i = size - 1; i >= 0; i--) {
        if (dir[i] == '/') {

            /* 绝对路径的情况下，/的上级还是/，/xxx 的上级是/ */
            if (is_abs && i == 0) {
                return;
            } else {
                dir[i] = '\0';
                return;
            }
        }
    }

    /* 能走到这儿的肯定是相对路径 */
    dir[0] = '\0';
}

static void base_name(const char* path, char* name)
{
    /* 暂时这样实现 */
    char dir_path[FS_MAX_FILE_PATH];
    dir_name(path, dir_path);

    int dir_size = strlen(dir_path);
    strncpy(name, path + dir_size, DIR_ITEM_NAME_SIZE);
}