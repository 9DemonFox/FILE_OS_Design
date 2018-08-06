#include "fs.h"

#include "fs_def.h"
#include "fulfs/fulfs.h"
#include "memory/alloc.h"
#include "utils/path.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>


struct fs_operate_functions_s g_operate_functions[FS_TYPE_TOTAL];
//根据不同的文件类型来扩展
//

struct dev_fsctrl_s g_device_filesystem['z' - 'a' + 1];
//声明一堆文件控制块
//会存上吗?

void fs_init(void)//文件系統初始化
{
    FS_OPERATE_FUNCTIONS_SET(g_operate_functions[FS_TYPE_FULFS], fulfs);
    //通过预定义页面初始化操作集合
    //这里实现文件系统到具体文件系统操作的赋予

    for (int i = 0; i <= 'z' - 'a'; i++) {
        g_device_filesystem[i].fs_type = FS_TYPE_NULL;
    }
}


/************************/


bool fs_mount(device_handle_t device, char drive_letter, int fs_type)
{
    drive_letter = tolower(drive_letter);//大写转小写 A->a
    if (!('a' <= drive_letter && drive_letter <= 'z')) {
        return false;
    }

    if (!(0 <=fs_type && fs_type < FS_TYPE_TOTAL)) {
        return false;
    }
    //文件系统错误

    for (int i = 0; i <= 'z' - 'a'; i++) {
        if (g_device_filesystem[i].fs_type != FS_TYPE_NULL &&
            g_device_filesystem[i].device == device) {
            return false;
            //分配一个设备
        }
    }

    if (g_device_filesystem[drive_letter - 'a'].fs_type == FS_TYPE_NULL) {
        //挂载设备成功
        g_device_filesystem[drive_letter - 'a'].fs_type = fs_type;
        g_device_filesystem[drive_letter - 'a'].device = device;
        g_device_filesystem[drive_letter - 'a'].fs_ctrl = g_operate_functions[fs_type].filesystem_new(device);
        //从磁盘的第一个扇区加载数据，即加载超级块
        g_device_filesystem[drive_letter - 'a'].opfuncs = &g_operate_functions[fs_type];
        return true;
    } else {
        return false;
    }
}

bool fs_dev_fs_ctrl(char drive_letter, struct dev_fsctrl_s* ctrl)
{
    drive_letter = tolower(drive_letter);
    if (!('a' <= drive_letter && drive_letter <= 'z')) {
        return false;
    }

    if (g_device_filesystem[drive_letter- 'a'].fs_type != FS_TYPE_NULL) {
        *ctrl = g_device_filesystem[drive_letter- 'a'];
        return true;
    } else {
        return false;
    }
}

static inline  struct dev_fsctrl_s* drive_letter_to_ctrl(char letter)
{
    return &g_device_filesystem[tolower(letter) - 'a'];//全局变量,返回
}


/************************/

static inline bool path_check(const char* path)
{
    return isalpha(path[0]) && path[1] == ':';
    //检测第一个为字母（盘符)第二个为:
}

static inline char path_drive_letter(const char* path)
{
    return path[0];//返回参数中的盘符
}

/* 去除盘符以外的部分 */
static inline const char* path_remain(const char* path)
{
    return path + 2;
}

static inline struct dev_fsctrl_s* path_to_ctrl(const char* path) {
    //输出文件控制块
    //形参为路径
    return drive_letter_to_ctrl(path_drive_letter(path));//路径中的盘符
    //盘符到控制块
}


#define FS_MAX_FILE_FD UINT16_MAX

/* FIXME:似乎C语言全局数组默认就是NULL? 记不清楚了 */
struct {
    fs_file_t* file;
    char drive_letter;
} g_fs_files[FS_MAX_FILE_FD];

int fs_open(const char* path)
{

    char abspath[FS_MAX_FILE_PATH];//绝对路径
    fs_abs_path(path, abspath, FS_MAX_FILE_PATH);
    if (!path_check(abspath)) {
        return FS_ERROR;
    }

    for (int fd = 0; fd < FS_MAX_FILE_FD; fd++) {
        if (g_fs_files[fd].file == NULL) {
            //如果文件标识符为空，则分配一个文件标识符
            struct dev_fsctrl_s* ctrl = path_to_ctrl(abspath);//路径生成文件控制表
            //获取一个设备到文件控制的块 包含文件信息 文件操作接口 设备以及
            fs_file_t* file = ctrl->opfuncs->open(ctrl->device, ctrl->fs_ctrl, path_remain(abspath));
            //根据当前的设备号，返回文件系统基本信息
            //再调用底层获取基本文件类型
            //包括设备号 i节点 内存i节点 超级块 块地址和块偏移地址（current）
            if (file == NULL) {
                return FS_ERROR;
            }

            g_fs_files[fd].file = file;
            g_fs_files[fd].drive_letter = path_drive_letter(abspath);//设备盘符
            return fd;//返回文件标识符
        }
    }
    return FS_ERROR;
}

void fs_close(int fd)
{
    if (fd < FS_MAX_FILE_FD && g_fs_files[fd].file != NULL) {
        struct dev_fsctrl_s* ctrl = drive_letter_to_ctrl(g_fs_files[fd].drive_letter);
        ctrl->opfuncs->close(g_fs_files[fd].file);
    }

}

int fs_read(int fd, char* buf, int count)
{
    if (fd < FS_MAX_FILE_FD && g_fs_files[fd].file != NULL) {
    struct dev_fsctrl_s* ctrl = drive_letter_to_ctrl(g_fs_files[fd].drive_letter);
    return ctrl->opfuncs->read(g_fs_files[fd].file, buf, count);
    } else {
        return FS_ERROR;
    }
}
int fs_write(int fd, const char* buf, int count)
{
    if (fd < FS_MAX_FILE_FD && g_fs_files[fd].file != NULL) {
        //检测文件标识符准确性
        struct dev_fsctrl_s* ctrl = drive_letter_to_ctrl(g_fs_files[fd].drive_letter);
        //设备号转文件控制块号
        //传入下层具体文件层操作
        return ctrl->opfuncs->write(g_fs_files[fd].file, buf, count);
    } else {
        return FS_ERROR;
    }
}

bool fs_ftruncate(int fd, fs_off_t size)
{
    if (fd < FS_MAX_FILE_FD && g_fs_files[fd].file != NULL) {
        struct dev_fsctrl_s* ctrl = drive_letter_to_ctrl(g_fs_files[fd].drive_letter);
        return ctrl->opfuncs->ftruncate(g_fs_files[fd].file, size);
    } else {
        return FS_ERROR;
    }
}

fs_off_t fs_lseek(int fd, fs_off_t off, int where)
{
    if (fd < FS_MAX_FILE_FD && g_fs_files[fd].file != NULL) {
        struct dev_fsctrl_s* ctrl = drive_letter_to_ctrl(g_fs_files[fd].drive_letter);
        return ctrl->opfuncs->lseek(g_fs_files[fd].file, off, where);
    } else {
        return FS_ERROR;
    }
}


int fs_mkdir(const char* path)
{//上层交付文件系统处理

    char abspath[FS_MAX_FILE_PATH];
    //绝对路径
    fs_abs_path(path, abspath, FS_MAX_FILE_PATH);
    //当前路径 绝对路径 最大路径长度
    //获取绝对路劲
    if (!path_check(abspath)) {
    //带盘符绝对路径检测
        return FS_ERROR;
    }
    //输出了全局的绝对路径
    //FIXME 无法在文件目录下创建文件目录 bingo
    struct dev_fsctrl_s* ctrl = path_to_ctrl(abspath);
    //盘符到文件的操作
    //传入的路径带盘符的，也就决定了文件系统
    if (ctrl->opfuncs->mkdir(ctrl->device, ctrl->fs_ctrl, path_remain(abspath))) {
        //参数 设备号
        //将参数传到mkdir中
        //传入去除盘符以外的路径
        //抽象文件系统交付fulfs文件系统操作
        return FS_SUCCESS;
    } else {
        return FS_ERROR;//返回格式错误
    }
}

int fs_rmdir(const char* path)
{
    char abspath[FS_MAX_FILE_PATH];
    fs_abs_path(path, abspath, FS_MAX_FILE_PATH);

    if (!path_check(abspath)) {
        return FS_ERROR;
    }

    struct dev_fsctrl_s* ctrl = path_to_ctrl(abspath);
    if (ctrl->opfuncs->rmdir(ctrl->device, ctrl->fs_ctrl, path_remain(abspath))) {
        return FS_SUCCESS;
    } else {
        return FS_ERROR;
    }
}

int fs_link(const char* src_path, const char* new_path)
{
    char src_abspath[FS_MAX_FILE_PATH];
    fs_abs_path(src_path, src_abspath, FS_MAX_FILE_PATH);

    char new_abspath[FS_MAX_FILE_PATH];
    fs_abs_path(new_path, new_abspath, FS_MAX_FILE_PATH);

    if (!path_check(src_abspath) || !path_check(new_abspath)) {
        return FS_ERROR;
    }

    if ((path_drive_letter(src_abspath)) != path_drive_letter(new_abspath)) {
        return FS_ERROR;
    }

    struct dev_fsctrl_s* ctrl = path_to_ctrl(src_abspath);
    if (ctrl->opfuncs->link(ctrl->device, ctrl->fs_ctrl, path_remain(src_abspath), path_remain(new_abspath))) {
        return FS_SUCCESS;
    } else {
        return FS_ERROR;
    }
}
int fs_unlink(const char* path)
{
    char abspath[FS_MAX_FILE_PATH];
    fs_abs_path(path, abspath, FS_MAX_FILE_PATH);

    if (!path_check(abspath)) {
        return FS_ERROR;
    }
    struct dev_fsctrl_s* ctrl = path_to_ctrl(abspath);
    if (ctrl->opfuncs->unlink(ctrl->device, ctrl->fs_ctrl, path_remain(abspath))) {
        return FS_SUCCESS;
    } else {
        return FS_ERROR;
    }
}

int fs_symlink(const char* src_path, const char* new_path)
{
    char src_abspath[FS_MAX_FILE_PATH];
    fs_abs_path(src_path, src_abspath, FS_MAX_FILE_PATH);

    char new_abspath[FS_MAX_FILE_PATH];
    fs_abs_path(new_path, new_abspath, FS_MAX_FILE_PATH);

    if (!path_check(src_abspath) || !path_check(new_abspath)) {
        return FS_ERROR;
    }

    if ((path_drive_letter(src_abspath)) != path_drive_letter(new_abspath)) {
        return FS_ERROR;
    }

    struct dev_fsctrl_s* ctrl = path_to_ctrl(src_abspath);
    if (ctrl->opfuncs->symlink(ctrl->device, ctrl->fs_ctrl, path_remain(src_abspath), path_remain(new_abspath))) {
        return FS_SUCCESS;
    } else {
        return FS_ERROR;
    }
}
int fs_readlink(const char *path, char *buf, size_t size)
{
    char abspath[FS_MAX_FILE_PATH];
    fs_abs_path(path, abspath, FS_MAX_FILE_PATH);

    if (!path_check(abspath)) {
        return FS_ERROR;
    }
    struct dev_fsctrl_s* ctrl = path_to_ctrl(abspath);
    if (ctrl->opfuncs->readlink(ctrl->device, ctrl->fs_ctrl, path_remain(abspath), buf, size)) {
        return FS_SUCCESS;
    } else {
        return FS_ERROR;
    }
    return FS_ERROR;
}



int fs_stat(const char *path, struct fs_stat *buf)
//传入信息 文件路径 文件标准信息
{
    char abspath[FS_MAX_FILE_PATH];//绝对地址
    fs_abs_path(path, abspath, FS_MAX_FILE_PATH);

    if (!path_check(abspath)) {//检测第一个为盘符 第二个为:
        return FS_ERROR;
    }
    struct dev_fsctrl_s* ctrl = path_to_ctrl(abspath);
    //通过盘符找到相应的操作函数
    //通过该操作函数和路径获取文件信息
    if (ctrl->opfuncs->stat(ctrl->device, ctrl->fs_ctrl, path_remain(abspath), buf)) {
        //path_remain保留去除盘符的路径`
        //传入去除盘符的路径
        // buf存放文件信息
        return FS_SUCCESS;
    } else {
        return FS_ERROR;
    }
}


struct _fs_dir_wrapper_s
{
    fs_dir_t* dir;
    char drive_letter;//盘符
};

FS_DIR* fs_opendir(const char *path)
{
    char abspath[FS_MAX_FILE_PATH];
    fs_abs_path(path, abspath, FS_MAX_FILE_PATH);

    if (!path_check(abspath)) {
        return NULL;
    }


    struct dev_fsctrl_s* ctrl = path_to_ctrl(abspath);//通过路径获取控制对象
    fs_dir_t* dir = ctrl->opfuncs->opendir(ctrl->device, ctrl->fs_ctrl, path_remain(abspath));
    if (dir == NULL) {
        return NULL;
    }

    FS_DIR* dir_wrapper = FT_NEW(FS_DIR, 1);
    dir_wrapper->dir = dir;
    dir_wrapper->drive_letter = path_drive_letter(abspath);
    //地柜调用
    return dir_wrapper;
}

int fs_readdir(FS_DIR* dir, char* name)
{//读出当前目录的文件项
    if (dir != NULL) {//如果文件不为空
        struct dev_fsctrl_s* ctrl = drive_letter_to_ctrl(dir->drive_letter);
        if (ctrl->opfuncs->readdir(dir->dir, name)) {
            return FS_SUCCESS;
        } else {
            return FS_ERROR;
        }
    } else {
        return FS_ERROR;
    }
}

void fs_closedir(FS_DIR* dir)
{
    if (dir != NULL) {
        struct dev_fsctrl_s* ctrl = drive_letter_to_ctrl(dir->drive_letter);
        ctrl->opfuncs->closedir(dir->dir);
        ft_free(dir);
    }
}

int fs_format(device_handle_t device, int sectors_per_block, int fs_type)
{
    if (!(0 <= fs_type && fs_type < FS_TYPE_TOTAL)) {
        return FS_ERROR;
    }
    //文件系统类型，未来扩展

    if (g_operate_functions[fs_type].format(device, sectors_per_block)) {
        // 设备号 扇区簇数
        //当前文件系统的初始化 参数
        return FS_SUCCESS;
    } else {
        return FS_ERROR;
    }
}


fs_size_t fs_filesystem_used_size(char drive_letter)
{
    struct dev_fsctrl_s ctrl;
    if (fs_dev_fs_ctrl(drive_letter, &ctrl)) {
        return ctrl.opfuncs->filesystem_used_size(ctrl.fs_ctrl);
    } else {
        return FS_ERROR;
    }
}
fs_size_t fs_filesystem_total_size(char drive_letter)
{
    struct dev_fsctrl_s ctrl;
    if (fs_dev_fs_ctrl(drive_letter, &ctrl)) {
        return ctrl.opfuncs->filesystem_total_size(ctrl.fs_ctrl);
    } else {
        return FS_ERROR;
    }
}

char g_current_dir[FS_MAX_FILE_PATH] = "";//设当前目录为全局变量

char* fs_getcwd(char *buffer,size_t size)
//传入参数 path 和size
//返回参数*buffer
//返回当前目录
{
    if (strlen(g_current_dir) <= 0 || size < 1) {
        return NULL;//如果全局变量为空
    } else {
        strncpy(buffer, g_current_dir, size - 1);//全局变量当前目录
        buffer[size - 1] = '\0';
    }
    return buffer;//缓冲区内容，写入截支止符
}

int fs_chdir(const char* path)
{
    struct fs_stat st;//申请一个文件结构体
    if (fs_stat(path, &st) != FS_SUCCESS) {
        return FS_ERROR;
    }

    if (st.st_mode != FS_S_IFDIR) {
        return FS_ERROR;
    }

    char abspath[FS_MAX_FILE_PATH];
    if (fs_abs_path(path, abspath, FS_MAX_FILE_PATH) == NULL) {
        return FS_ERROR;
    }

    strncpy(g_current_dir, abspath, FS_MAX_FILE_PATH - 1);
    g_current_dir[FS_MAX_FILE_PATH - 1] = '\0';
    return FS_SUCCESS;
}

/* FIXME: 目前对于带盘符的路径的一些约定有些混乱，先暂时这样吧 */
char* fs_abs_path(const char* path, char* abs_path, size_t size)
//当前路径 绝对路径 路径长度
//
{
    if (path[1] != ':') {//如果第二个是: 即出现A:
        if (fs_getcwd(abs_path, size) == NULL) {
            return NULL;
        }//如果不是带盘符的绝对地址，则赋予盘符绝对地址

        path_join(abs_path, size, path);//将当前路径和新建文件夹合并

    } else {
        strncpy(abs_path, path, size - 1);
        abs_path[size - 1] = '\0';
    }

    path_simplify(abs_path);

    assert(abs_path[1] == ':');//断言第二个为A

    if (abs_path[2] == '\0' ) {
        abs_path[2] = '/';
        abs_path[3] = '\0';
    }
    return abs_path;
}
