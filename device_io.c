#include "device_io.h"
#include<stdio.h>
#include<string.h>

#include"memory/alloc.h"
#include"utils/sys.h"
#include"utils/log.h"


struct _device_s{
//设备包括 路径 文件指针 扇区数目
// 将文件虚拟成设备
    char* path;
    FILE* fp;
    sector_no_t section_count;//扇区数目
};

#define MAX_DEVICE_COUNT 1024//最大设备数目
struct _device_s* device_handle[MAX_DEVICE_COUNT] = {NULL};//结构体数组

int device_add(const char* path)
//路径为形参
{
    /* 已经挂载的文件就不允许别人挂载了 */
    for (int i = 0; i < MAX_DEVICE_COUNT; i++) {
        if (device_handle[i] != NULL && strcmp(device_handle[i]->path, path) == 0) {//设备的句柄为空，或者设备被其他目录挂载
            log_info("%s文件已经被当成设备挂载了，请先卸载", path);
            return DEVICE_IO_ERROR;
        }
    }
    for (int i = 0; i < MAX_DEVICE_COUNT; i++) {//循环查找合适的句柄
        if (device_handle[i] == NULL) {
            device_handle[i] = FT_NEW(struct _device_s, 1);

            device_handle[i]->path = FT_NEW(char, strlen(path) + 1);
            strcpy(device_handle[i]->path, path);

            device_handle[i]->fp = fopen(path, "r+b");
            device_handle[i]->section_count = ft_filesize_from_fp(device_handle[i]->fp) /  BYTES_PER_SECTOR;

            return i;
        }
    }

    return DEVICE_IO_ERROR;
}


void device_del(device_handle_t handle)
//句柄
{
    if (handle < MAX_DEVICE_COUNT && handle >= 0 && device_handle[handle] != NULL) {
        fclose(device_handle[handle]->fp);//关闭设备
        ft_free(device_handle[handle]->path);//将设备卸载
        ft_free(device_handle[handle]);//存储设备文件卸载
        device_handle[handle] = NULL;//初始化
    }
}

static struct _device_s* handle_to_struct(device_handle_t handle)//句柄到结构体
{
    if (!(handle < MAX_DEVICE_COUNT && handle >= 0 && device_handle[handle] != NULL)) {
        return NULL;
    } else {
        return device_handle[handle];
    }
}

int device_read(device_handle_t handle, sector_no_t sector_no, int count, char* buf)
//设备层 只负责处理扇区
//设备号句柄（0） 扇区起始位置 数目 缓冲区存数据
//设备层读写
{
    struct _device_s* device = handle_to_struct(handle);
    if (device == NULL) {
        return DEVICE_IO_ERROR;
    }

    size_t offset = sector_no * BYTES_PER_SECTOR;
    //每个扇区偏移地址=扇区号×扇区大小
    
    if (!(sector_no + count <= device->section_count)) {
        count = device->section_count - sector_no;
    }

    if(fseek(device->fp, offset, SEEK_SET) != 0) {
        return DEVICE_IO_ERROR;
    }

    fread(buf, BYTES_PER_SECTOR, count, device->fp);

    if (ferror(device->fp)) {
        return DEVICE_IO_ERROR;
    } else {
        return count;
    }
}


int device_write(device_handle_t handle, sector_no_t sector_no, int count, const char* buf)
{//写入设备
    struct _device_s* device = handle_to_struct(handle);
    if (device == NULL) {
        return DEVICE_IO_ERROR;
    }


    size_t offset = sector_no * BYTES_PER_SECTOR;
    //计算偏移地址
    if (!(sector_no + count <= device->section_count)) {
        count = device->section_count - sector_no;
    }

    if(fseek(device->fp, offset, SEEK_SET) != 0) {
        return DEVICE_IO_ERROR;
    }

    fwrite(buf, BYTES_PER_SECTOR, count, device->fp);


    if (ferror(device->fp)) {
        return DEVICE_IO_ERROR;
    } else {
        return count;
    }
}

int device_section_count(device_handle_t handle)
{//返回设备的扇区计数
    struct _device_s* device = handle_to_struct(handle);
    if (device == NULL) {
        return 0;
    } else {
        return device->section_count;
    }
}

