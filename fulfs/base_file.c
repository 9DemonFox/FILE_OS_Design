#include "base_file.h"

#include "mem_inode.h"
#include "block.h"
#include "base_block_file.h"
#include "../utils/math.h"
#include "../utils/log.h"
#include <string.h>
#include <time.h>

#include <assert.h>


/* TODO 关于时间的部分没管它 */

static bool base_file_del(device_handle_t device, superblock_t* sb, inode_no_t inode_no);


bool base_file_open(base_file_t* base_file, device_handle_t device, superblock_t* sb, inode_no_t inode_no)
{
    //打开文件块 把信息传入base_file 设备号 文件超级块 i节点编号
    //一切皆是文件
    base_file->device = device;
    base_file->inode_no = inode_no;
    dev_inode_ctrl_t dev_inode_ctrl;
    dev_inode_ctrl_init_from_superblock(&dev_inode_ctrl, device, sb);//前者包数据块大小，开始地址,数量
    base_file->sb = sb;
    bool success = mem_inode_get(&dev_inode_ctrl, inode_no, &(base_file->mem_inode));
    //底层文件层到块文件层
    //获取存储inode的节点
    if (!success) {//获取mem_inode
        return false;
    }

    base_file->current.current_block_relative = 0;//获得块地址
    base_file->current.current_offset = 0;//

    base_file->mem_inode->inode.accessed_time = time(NULL);//访问时间更新
    return true;
}

int base_file_mode(const base_file_t* base_file)
{
    return base_file->mem_inode->inode.mode;
}

fsize_t base_file_size(const base_file_t* base_file)
{//文件的内存i节点的节点大小
    return base_file->mem_inode->inode.size;
}

timestamp_t base_file_accessed_time(const base_file_t* base_file)
{
    return base_file->mem_inode->inode.accessed_time;
}

timestamp_t base_file_modified_time(const base_file_t* base_file)
{
    return base_file->mem_inode->inode.modified_time;
}

timestamp_t base_file_created_time(const base_file_t* base_file)
{
    return base_file->mem_inode->inode.created_time;
}

bool base_file_seek(base_file_t* base_file, fsize_t offset)
{
    assert(offset <= base_file->mem_inode->inode.size);

    block_no_t block_relative = offset / superblock_block_size(base_file->sb);
    //相对块偏移=偏移除去块大小
    base_file->current.current_block_relative = block_relative;
    //偏移块数为取除数的整数 如525/512=1
    base_file->current.current_offset = offset % superblock_block_size(base_file->sb);
    //文件偏移为取余数3
    return true;
}

fsize_t base_file_tell(const base_file_t* base_file)
{//返回基本文件项的地址
    return base_file->current.current_block_relative * superblock_block_size(base_file->sb)
        + base_file->current.current_offset;
        //返回基本文件的绝对地址地址
}

int base_file_read(base_file_t* base_file, char* buf, int count)
//文件从磁盘中读出
{
    int sectors_per_block = superblock_sectors_per_block(base_file->sb);

    /* 保证接下来的count没有超过文件大小 */
    if (base_file_size(base_file) - base_file_tell(base_file) < (fsize_t)count) {
        count = base_file_size(base_file) - base_file_tell(base_file);
    }

    char block_buf[MAX_BYTES_PER_BLOCK];
    int readed_count = 0;
    while (readed_count < count) {
        block_no_t current_block;
        bool success = base_block_file_locate(base_file->device, base_file->sb, &(base_file->mem_inode->inode),
                                              base_file->current.current_block_relative, &current_block);
        if (!success) {
            log_debug("定位文件block失败: %d号设备, 文件inode号%d, 相对块号%d\n", base_file->device,
                      base_file->inode_no, base_file->current.current_block_relative);
            return readed_count;
        }

        success = block_read(base_file->device, sectors_per_block, current_block, block_buf);
        if (!success) {
            log_debug("读取块失败: %d号设备, 文件inode号%d, 块号%d\n", base_file->device, base_file->inode_no, current_block);
            return readed_count;
        }

        int should_read_size = min_int(sectors_per_block * BYTES_PER_SECTOR - base_file->current.current_offset, count - readed_count);

        memcpy(buf + readed_count, block_buf + base_file->current.current_offset, should_read_size);
        readed_count += should_read_size;

        base_file_seek(base_file, base_file_tell(base_file) + should_read_size);
    }

    return readed_count;
}

int base_file_write(base_file_t* base_file, const char* buf, int count)
// 在块层写 传入参数文件标识
//一簇一簇的写，一簇一簇的分配
{
    int sectors_per_block = superblock_sectors_per_block(base_file->sb);
    //扇区每簇
    char block_buf[MAX_BYTES_PER_BLOCK];
    //申请一个缓冲区 缓冲区大小是每簇可能最大的
    int writed_count = 0;//写文件计数
    while (writed_count < count) {//直到所有文件都写完
        block_no_t current_block;
        //当前块号
        if (base_file->current.current_block_relative >= count_groups(base_file_size(base_file), sectors_per_block * BYTES_PER_SECTOR)) {
            //当文件的块偏移超过了所有块的数量 则失败
            bool success = base_block_file_push_block(base_file->device, base_file->sb, &base_file->mem_inode->inode, &current_block);
            if (!success) {
                log_debug("分配新block失败: %d号设备, 文件inode号%d, 相对块号%d\n", base_file->device,
                          base_file->inode_no, base_file->current.current_block_relative);
                return writed_count;
            }
        } else {
            bool success = base_block_file_locate(base_file->device, base_file->sb, &base_file->mem_inode->inode,
                                                  base_file->current.current_block_relative, &current_block);
            //传入参数设备号 超级快 内存i节点
            // 传回文件块的相对偏移地址
            if (!success) {
                log_debug("定位文件block失败: %d号设备, 文件inode号%d, 相对块号%d\n", base_file->device,
                          base_file->inode_no, base_file->current.current_block_relative);
                return writed_count;
            }
        }

        bool success = block_read(base_file->device, sectors_per_block, current_block, block_buf);
        //将文件内容
        //为什么是读？？？？？
        //在创建文件夹时用于不覆盖前边的文件夹项！
        if (!success) {
            return writed_count;
        }

        int should_write_size = min_int(sectors_per_block * BYTES_PER_SECTOR - base_file->current.current_offset,
                                       count - writed_count);
                                       //每块的字节大小 当前偏移减去最后剩下的
                                       //应该写入的技术为两者最小值

        memcpy(block_buf + base_file->current.current_offset, buf + writed_count, should_write_size);
        //buf+write_count当前已经写到的地址
        success = block_write(base_file->device, sectors_per_block, current_block, block_buf);
        if (!success) {
            return writed_count;
        }

        writed_count += should_write_size;//加上应该写的字节数，用于处理不满一个簇大小的情况

        fsize_t will_pos = base_file_tell(base_file) + should_write_size;
        if (will_pos > base_file_size(base_file)) {
            base_file->mem_inode->inode.size = will_pos;
        }
        base_file_seek(base_file, will_pos);
    }

    base_file->mem_inode->inode.modified_time = time(NULL);//写入修改时间

    return writed_count;//返回写入计数
}

bool base_file_close(base_file_t* base_file)
{
    dev_inode_ctrl_t dev_inode_ctrl;
    dev_inode_ctrl_init_from_superblock(&dev_inode_ctrl, base_file->device, base_file->sb);

    if (!superblock_dump(base_file->device, base_file->sb)) {
        return false;
    }

    return mem_inode_put(&dev_inode_ctrl, base_file->mem_inode);
}

bool base_file_create(device_handle_t device, superblock_t* sb, int mode, inode_no_t* p_inode_no)
//设备句柄 超级块数据结构 状态
//一切皆文件，文件夹也是文件
{
    dev_inode_ctrl_t dev_inode_ctrl;
    dev_inode_ctrl_init_from_superblock(&dev_inode_ctrl, device, sb);

    bool success = inode_alloc(&dev_inode_ctrl, p_inode_no);
    if (!success) {
        return false;
    }

    inode_t inode;
    success = inode_load(&dev_inode_ctrl, *p_inode_no, &inode);
    if (!success) {
        return false;
    }

    inode.mode = mode;
    inode.size = 0;
    inode.link_count = 1;

    inode.accessed_time = time(NULL);
    inode.modified_time = time(NULL);
    inode.created_time = time(NULL);

    sb->used_inode_count++;

    if (!superblock_dump(device, sb)) {
        return false;
    }
    success = inode_dump(&dev_inode_ctrl, *p_inode_no, &inode);
    if (!success) {
        return false;
    }

    return true;
}

bool base_file_ref(device_handle_t device, superblock_t* sb, inode_no_t inode_no)
{
    dev_inode_ctrl_t dev_inode_ctrl;
    dev_inode_ctrl_init_from_superblock(&dev_inode_ctrl, device, sb);

    mem_inode_t* mem_inode;
    bool success = mem_inode_get(&dev_inode_ctrl, inode_no, &mem_inode);
    if (!success) {
        return false;
    }

    mem_inode->inode.link_count++;

    return mem_inode_put(&dev_inode_ctrl, mem_inode);
}

bool base_file_unref(device_handle_t device, superblock_t* sb, inode_no_t inode_no)
{
    dev_inode_ctrl_t dev_inode_ctrl;
    dev_inode_ctrl_init_from_superblock(&dev_inode_ctrl, device, sb);

    mem_inode_t* mem_inode;
    bool success = mem_inode_get(&dev_inode_ctrl, inode_no, &mem_inode);
    if (!success) {
        return false;
    }

    mem_inode->inode.link_count--;
    if (mem_inode->inode.link_count > 0) {
        return mem_inode_put(&dev_inode_ctrl, mem_inode);
    } else {//如果引用的次数为零就删除文件
        return base_file_del(device, sb, inode_no);
    }
}

bool base_file_truncate(base_file_t* base_file, fsize_t size)
{//基本文件截断
    /* 这个函数要保证出错时不破坏完整性 */
    if (base_file->mem_inode->inode.size > size) {//文件大小

        int block_size = superblock_block_size(base_file->sb);

        block_no_t block_num = count_groups(base_file->mem_inode->inode.size, block_size);//计算块数
        block_no_t should_block_num = count_groups(size, block_size);

        for (block_no_t i = 0; i < block_num - should_block_num; i++) {

            /* 那么，完整性的责任转由这个函数保证 */
            bool success = base_block_file_pop_block(base_file->device, base_file->sb, &(base_file->mem_inode->inode));
            if (!success) {
                return false;
            }

            base_file->mem_inode->inode.size -= block_size;
        }

        base_file->mem_inode->inode.size = size;

        base_file->mem_inode->inode.modified_time = time(NULL);
        return true;

    } else {
        return true;
    }
}


int base_file_ref_count(base_file_t* base_file)
{
    return base_file->mem_inode->inode.link_count;
}

bool base_file_block_count(base_file_t* base_file, long* p_count)
{
    return base_block_file_block_count(base_file->device, base_file->sb, &base_file->mem_inode->inode, p_count);
}

/*********************************删除文件*/
static bool base_file_del(device_handle_t device, superblock_t* sb, inode_no_t inode_no)
{
    base_file_t base_file;
    bool success = base_file_open(&base_file, device, sb, inode_no);
    if (!success) {
        return false;
    }

    bool has_fp = base_file.mem_inode->ref_count > 0;

    /* 释放block */
    success = base_file_truncate(&base_file, 0);
    if (!success) {
        return false;
    }
    base_file_close(&base_file);

    if (!has_fp) {
        return false;
    }


    /* 释放inode */
    dev_inode_ctrl_t dev_inode_ctrl;
    dev_inode_ctrl_init_from_superblock(&dev_inode_ctrl, device, sb);
    inode_free(&dev_inode_ctrl, inode_no);

    sb->used_inode_count--;
    //写会sb
    bool superblock_dump(device,sb);//写回sb

    return true;
}

