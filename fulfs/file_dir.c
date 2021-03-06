#include "file_dir.h"

#include "base_file.h"
#include "../utils/math.h"
#include "../utils/log.h"
#include "../memory/alloc.h"
#include "../utils/path.h"
#include <string.h>
#include <assert.h>

#define FILE_MAX_NAME 14

static bool dir_locate(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name, bool* p_exist, inode_no_t* p_no);
static bool dir_add(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name, inode_no_t no);
static bool dir_del(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name);
static bool dir_tree_locate(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t start, const char* relative_path, bool* p_exist, inode_no_t* p_no);
static bool dir_roottree_locate(device_handle_t device, fulfs_filesystem_t* fs, const char* path, bool* p_exist, inode_no_t* p_no);

/* -------------------- */
#define DIR_ITEM_NAME_SIZE 14
#define DIR_ITEM_SIZE (DIR_ITEM_NAME_SIZE + sizeof(inode_no_t))

struct dir_item_s {
    char name[DIR_ITEM_NAME_SIZE + 1];//文件项只有文件名字和i节点号
    block_no_t inode_no;
};

static void dir_item_load_from_bin(struct dir_item_s* item, const char* bin);
static void dir_item_dump_to_bin(const struct dir_item_s* item, char* bin);

/* -------------------- */


static bool fulfs_file_init(fulfs_file_t* file, device_handle_t device, fulfs_filesystem_t* fs, const char* path);

fulfs_file_t* fulfs_open(device_handle_t device, fulfs_filesystem_t* fs, const char* path)
{
    fulfs_file_t* file = FT_NEW(fulfs_file_t, 1);
    if (!fulfs_file_init(file, device, fs, path)) {
        return NULL;
    }
    return file;
}

static bool fulfs_file_init(fulfs_file_t* file, device_handle_t device, fulfs_filesystem_t* fs, const char* path)
{
    char dir_path[FS_MAX_FILE_PATH];
    char name[FILE_MAX_NAME];

    path_basename(path, name, FILE_MAX_NAME);
    path_dirname(path, dir_path);
    /*修正*/
    /*未知bug*/
    /* 定位出目录所在的inode */
    bool exist;
    inode_no_t dir_no;
    bool success = dir_roottree_locate(device, fs, dir_path, &exist, &dir_no);
    if (!success || !exist) {
        return false;
    }

    inode_no_t file_no;
    success = dir_locate(device, fs, dir_no, name, &exist, &file_no);
    if (!success) {
        return false;
    }

    /* 不存在就建立新文件 */
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
    ft_free(file);
}

int fulfs_read(fulfs_file_t* file, char* buf, int count)
{
    return base_file_read(&file->base_file, buf, count);
}

int fulfs_write(fulfs_file_t* file, const char* buf, int count)
{//文件层传入下层操作
    return base_file_write(&file->base_file, buf, count);
}


bool fulfs_ftruncate(fulfs_file_t* file, fs_off_t size)
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
//传过来的参数
//设备 文件系统（超级块) 路径(除去盘符所有)
//上层文件系统交付下层fulfs文件系统处理
//不能创建子目录
{

    char dir_path[FS_MAX_FILE_PATH];//文件夹路径
    char name[FILE_MAX_NAME];//文件名字
    path_basename(path, name, FILE_MAX_NAME);//求得文件名
    path_dirname(path, dir_path);//求出所在的文件夹路径
    //路径复制到文件价路径
    /* 定位出目录所在的inode */
    /* path /test0 name test0 dir_path /*/
    bool exist;
    inode_no_t dir_no;//i节点编号
    bool success = dir_roottree_locate(device, fs, dir_path, &exist, &dir_no);
    //参数 设备号 文件系统 文件夹路径
    // exsit（传回参数)  i节点编号//定位出文件夹位置
    if (!success) {
        return false;
    }


    /* 是否存在同名文件 */
    inode_no_t file_no;
    dir_locate(device, fs, dir_no, name, &exist, &file_no);//存在位和文件目录号（根目录为0）
    if (exist) {
        printf("已存在同名文件！\n");
        return false;
    }

    success = base_file_create(device, &fs->sb, MODE_DIR, &file_no);
    if (!success) {//创建文件 传入参数： 设备号 传回superblock 文件类型 传回文件参数
        return false;
    }

    success =  dir_add(device, fs, dir_no, name, file_no);
    //传入设备号 文件系统信息 文件夹编号 文件夹名字 文件夹编号
    /* FIXME: 这里失败了应该要把新建立的文件删除 */
    //并且将文件加入到文件目录
    if (!success) {
        return false;
    }

    return true;
}

bool fulfs_rmdir(device_handle_t device, fulfs_filesystem_t* fs, const char* path)
{//删除文件夹
    char dir_path[FS_MAX_FILE_PATH];
    char name[FILE_MAX_NAME];

    path_basename(path, name, FILE_MAX_NAME);//从path获得姓名
    path_dirname(path, dir_path);//从path获得路径
    /*未知错误*/
    /* 定位出目录所在的inode */
    bool exist;
    inode_no_t parent_dir_no;

    bool success = dir_roottree_locate(device, fs, dir_path, &exist, &parent_dir_no);
    // 传入设备号 文件控制 文件夹路径名 存在标志 父亲文件夹节点的编号
    if (!success) {
        return false;
    }


    /* 是否存在同名文件 */
    inode_no_t dir_no;
    success = dir_locate(device, fs, parent_dir_no, name, &exist, &dir_no);
    if (!success && !exist) {
        return false;
    }

    base_file_t base_file;
    success = base_file_open(&base_file, device, &fs->sb, dir_no);
    if (!success) {
        return false;
    }

    bool is_dir = (base_file_mode(&base_file) == MODE_DIR);
    bool is_empty = (base_file_size(&base_file) == 0);//文件项为空
    base_file_close(&base_file);

    if (is_dir && is_empty) {
        success = dir_del(device, fs, parent_dir_no, name);
        if (!success) {
            return false;
        }
        base_file_unref(device, &fs->sb, dir_no);
        return true;
    } else {
        return false;
    }
}

bool fulfs_link(device_handle_t device, fulfs_filesystem_t* fs, const char* src_path, const char* new_path)
{
    /* src文件所在的inode */
    bool exist;
    inode_no_t no;
    bool success = dir_roottree_locate(device, fs, src_path, &exist, &no);
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
        log_warning("不能给除真正文件之外的文件(一切都是文件)做硬链接: %s, %s\n", src_path, new_path);
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

    path_basename(new_path, name, FILE_MAX_NAME);
    path_dirname(new_path, dir_path);
    //未知错误
    inode_no_t dir_no;
    success = dir_roottree_locate(device, fs, dir_path, &exist, &dir_no);
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
    char dir_path[FS_MAX_FILE_PATH];
    char name[FILE_MAX_NAME];

    path_basename(path, name, FILE_MAX_NAME);
    path_dirname(path, dir_path);
    //未知错误
    bool exist;
    inode_no_t dir_no;
    bool success = dir_roottree_locate(device, fs, dir_path, &exist, &dir_no);
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
    /* FIXME: 暂时先这样判断文件是否存在 */
    if (!fulfs_stat(device, fs, src_path, NULL) || !fulfs_stat(device, fs, src_path, NULL)) {
        return false;
    }

    fulfs_file_t* file = fulfs_open(device, fs, new_path);
    if (file == NULL) {
        return false;
    }


    int write_size = strlen(new_path) + 1;
    int count = fulfs_write(file, new_path, write_size);
    if (count != write_size) {
        return false;
    }

    fulfs_close(file);

    return true;
}

bool fulfs_readlink(device_handle_t device, fulfs_filesystem_t* fs, const char *path, char *buf, size_t size)
{
    struct fs_stat st;
    if (!fulfs_stat(device, fs, path, &st)) {
        return false;
    }

    if (st.st_mode != FS_S_IFLNK) {
        return false;
    }

    fulfs_file_t* file = fulfs_open(device, fs, path);
    if (file == NULL) {
        return false;
    }


    int read_size = min_int(size - 1, st.st_size);
    int count = fulfs_read(file, buf, read_size);
    if (count != read_size) {
        return false;
    }
    buf[size] = '\0';

    fulfs_close(file);

    return true;
}


bool fulfs_stat(device_handle_t device, fulfs_filesystem_t* fs, const char *path, struct fs_stat *buf)//传入除去盘符的目录//相对于文件系统的绝对地址
{
    bool exist;
    inode_no_t no;//编号为0暂时
    bool success = dir_roottree_locate(device, fs, path, &exist, &no);
    //传入系统信息，路径，返回存在位和i节点号
    if (!success || !exist) {
        //printf("文件不存在");
        return false;
    }

    base_file_t base_file;
    success = base_file_open(&base_file, device, &fs->sb, no);//定位到目录项
    //打开文件并且返回一个文件控制块 basefile
    if (!success) {
        return false;
    }

    if (buf == NULL) {
        return true;
    }

    buf->st_ino = no;
    buf->st_nlink = base_file_ref_count(&base_file);
    buf->st_size = base_file_size(&base_file);
    long blocks;
    success = base_file_block_count(&base_file, &blocks);
    if (!success) {
        return false;
    }
    buf->st_blocks = blocks;
    buf->st_blksize = superblock_block_size(&fs->sb);
    buf->st_atime = base_file_accessed_time(&base_file);
    buf->st_mtime = base_file_modified_time(&base_file);
    buf->st_ctime = base_file_created_time(&base_file);
    //传过去文件信息
    int mode = base_file_mode(&base_file);
    if (mode == MODE_FILE) {//文件的类型 目录 文件 快捷方式....
        buf->st_mode = FS_S_IFREG;
    } else if (mode == MODE_DIR) {
        buf->st_mode = FS_S_IFDIR;
    } else if (mode == MODE_SYMBOL_LINK) {
        buf->st_mode = FS_S_IFLNK;
    } else {
        assert(false);
    }

    return true;
}


fulfs_dir_t* fulfs_opendir(device_handle_t device, fulfs_filesystem_t* fs, const char *path)
//设备号 文件系统信息(超级块) 路径
{
    fulfs_dir_t* dir = FT_NEW(fulfs_dir_t, 1);
    // fulfs文件系统的文件夹信息
    // 暂时只包含基本文件信息
    bool exist;
    inode_no_t no;//no用来传回inode编号
    bool success = dir_roottree_locate(device, fs, path, &exist, &no);//定位到目录
    if (!success || !exist) {
        return NULL;
    }

    success = base_file_open(&dir->base_file, device, &fs->sb, no);//找到当前文件夹后打开此文件夹
    if (!success) {
        return NULL;
    }

    return dir;//fulfs的目录对象，当前只包含基本文件信息
}

bool fulfs_readdir(fulfs_dir_t* dir, char* name)
{//通过文件类型和文件名字读取文件夹
    assert(base_file_size(&dir->base_file) % DIR_ITEM_SIZE == 0);

    if (base_file_tell(&dir->base_file) >= base_file_size(&dir->base_file)) {
        name[0] = '\0';
        return true;
    }

    char buf[DIR_ITEM_SIZE];
    int count = base_file_read(&dir->base_file, buf, DIR_ITEM_SIZE);//读取项目的数量
    if (count != DIR_ITEM_SIZE) {//校验是否是文件大小
        return false;
    }

    struct dir_item_s dir_item;
    dir_item_load_from_bin(&dir_item, buf);

    strcpy(name, dir_item.name);

    return true;
}

bool fulfs_closedir(fulfs_dir_t* dir)
{
    bool success = base_file_close(&dir->base_file);
    ft_free(dir);
    return success;
}

/*************************************/

static bool dir_locate(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t dir, const char* name, bool* p_exist, inode_no_t* p_no)
{//传过来的设备号 超级块 文件夹块 名字 存在标志 i节点编号
    base_file_t base_file;
    bool success = base_file_open(&base_file, device, &fs->sb, dir);//返回文件控制信息
    //传回base_file 传入设备 文件系统superBlock 以及文件夹信息存放的块即0
    //打开当前路径的文件夹，并且返回文件信息到base_file
    if (!success) {
        return false;
    }

    assert(base_file_mode(&base_file) == MODE_DIR);
    // 断言返回的是文件夹
    assert(base_file_size(&base_file) % DIR_ITEM_SIZE == 0);
    //断言文件的大小是文件项的整数倍
    char buf[DIR_ITEM_SIZE];
    while (base_file_tell(&base_file) < base_file_size(&base_file)) {//当偏移地址小于文件的地址时，循环度出内容到buf
        //base_file_tell当前文件的相对文件内偏移地址
        //在一个基本文件
        int count = base_file_read(&base_file, buf, DIR_ITEM_SIZE);//返回读到的
        //根据i节点找到文件夹所在块
        //将一簇信息读入buf中
        if (count != DIR_ITEM_SIZE) {
            base_file_close(&base_file);
            return false;
        }

        struct dir_item_s dir_item;
        dir_item_load_from_bin(&dir_item, buf);//文件项从二进制文件加载
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
// 设备句柄 文件系统(超级块信息) i节点编号 文件夹名字 i节点编号
 {

    assert(strlen(name) > 0);
    //断言那么>0为真

    base_file_t base_file;
    bool success = base_file_open(&base_file, device, &fs->sb, dir);
    if (!success) {
        return false;
    }

    assert(base_file_mode(&base_file) == MODE_DIR);//断言传到此处的文件类型是文件夹类型

    assert(base_file_size(&base_file) % DIR_ITEM_SIZE == 0);

    char buf[DIR_ITEM_SIZE];
    struct dir_item_s dir_item;
    strncpy(dir_item.name, name, DIR_ITEM_NAME_SIZE);
    dir_item.name[DIR_ITEM_NAME_SIZE] = '\0';
    dir_item.inode_no = no;
    dir_item_dump_to_bin(&dir_item, buf);

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
    bool success = base_file_open(&base_file, device, &fs->sb, dir);//
    if (!success) {
        return false;
    }

    assert(base_file_mode(&base_file) == MODE_DIR);
    assert(base_file_size(&base_file) % DIR_ITEM_SIZE == 0);

    char buf[DIR_ITEM_SIZE];
    while (base_file_tell(&base_file) < base_file_size(&base_file)) {//当前文件偏移比一个文件块小
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
            }//整理目录项

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
{//将目录项从二进制文件中（设备)中加载出来
    /* 二进制的数据里面，文件名称在达到14个字节的情况下，允许不带\0 */
    memcpy(&item->inode_no, bin, sizeof(inode_no_t));//从缓冲区中读取文件项的inode
    memcpy(item->name, bin  + sizeof(inode_no_t), DIR_ITEM_NAME_SIZE);
    //从缓冲区中读取文件夹名字
    //控制拷贝大小
    item->name[DIR_ITEM_NAME_SIZE] = '\0';//终止符
}

static void dir_item_dump_to_bin(const struct dir_item_s* item, char* bin)
{
    memcpy(bin, &item->inode_no, sizeof(inode_no_t));
    memcpy(bin  + sizeof(inode_no_t), item->name, DIR_ITEM_NAME_SIZE);
}

static bool dir_tree_locate(device_handle_t device, fulfs_filesystem_t* fs, inode_no_t start, const char* relative_path, bool* p_exist, inode_no_t* p_no)
{//设备 文件系统 根目录的inode号 0 相对地址（第一个不是/） 传回：存在位和inode编号
    assert(relative_path[0] != '/');
    //断言相对地址的第1位不为‘/’
    char name[DIR_ITEM_NAME_SIZE + 1] = {'\0'};
    //文件夹名字不能超过14个字符
    int count = 0;
    for (const char* p = relative_path; *p != '\0'; p++) {
        count++;//
        if (*(p + 1) == '/' || *(p + 1) == '\0') {
            //找到下一级目录或者结尾

            bool exist;
            inode_no_t no;
            strncpy(name, (p + 1) - count, min_int(DIR_ITEM_NAME_SIZE, count));
            //限制文件夹名字长度
            name[min_int(DIR_ITEM_NAME_SIZE, count)] = '\0';
            //no当前为0
            bool success = dir_locate(device, fs, start, name, &exist, &no);
            //通过文件名字打开文件
            //定位文件是否存在
            //存在返回其inode
            //否则返回不存在
            if (!success) {
                return false;
            }

            if (!exist) {
                *p_exist = false;
                return true;//如果不存在，则返回标志位不存在
            } else {
                start = no;
            }

            if (*(p + 1) == '/') {
                p++;
            }
            count = 0;
        }
    }

    *p_exist = true;//如果找到了就返回存在
    *p_no = start;//如果在当前目录查到了，就返回start
    return true;
}

static bool dir_roottree_locate(device_handle_t device, fulfs_filesystem_t* fs, const char* path, bool* p_exist, inode_no_t* p_no)
{//文件夹的根定位
    //设备号 文件系统 路径 存在标志? i节点编号
    // 根目录定位
    assert(path[0] == '/' || path[0] == '\0');//s断言path[0]==‘'‘或则’\0‘
    //将路径转化为'/'
    const char* relative_path = (path[0] == '/' ? path + 1 : path);//如果path[0]=='/'，path+1
    //如果开头为‘/’则把path右移一位s，否则指针右移
    //去除从左到右第一根斜杠
    return dir_tree_locate(device, fs, superblock_root_dir_inode(&fs->sb), relative_path, p_exist, p_no);
    // 参数 设备号 文件系统 文件系统的超级块(指向的文件根目录) 相对地址 存在位 节点编号
    //文件目录定位

}

