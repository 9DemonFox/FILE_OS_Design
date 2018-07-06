#include "filesystem.h"
#include "block.h"
#include "inode.h"
#include "superblock.h"
#include "data_block.h"
#include "base_file.h"

#include "../utils/math.h"
#include "../utils/log.h"
#include "../memory/alloc.h"


bool fulfs_format(device_handle_t device, int sectors_per_block)
//fulfs系統的初始化 sectors_per_block  1
{
    block_no_t block_count = device_section_count(device) / sectors_per_block;
    //总的block数目
    block_no_t inode_table = 1;//存放inode超级块
    /* inode 所占的簇数 */
    int inode_blocksize =  inode_block_count(sectors_per_block * BYTES_PER_SECTOR, INODE_MAX_COUNT);
    //inode所占的簇数
    block_no_t data_block = inode_table + inode_blocksize;
    //当前数据区格式化等于簇 第一扇区+inode扇区

    if (block_count  <= 0 ||
        data_block >= block_count) {
        log_debug("空间太小无法格式化");
        return false;
    }


    /* 初始化磁盘的inode区 */
    dev_inode_ctrl_t dev_inode_ctrl;//控制i节点的数据结构
    dev_inode_ctrl_init(&dev_inode_ctrl, device, sectors_per_block * BYTES_PER_SECTOR, inode_table, inode_blocksize);
    //磁盘控制块信息：设备 每个簇有多少个扇区 开始位置（超级块数目） inode数目
    inode_t inode;//数据结构
    inode_init(&inode);//初始化i节点 i.mode=0

    for (inode_no_t i = 0; i < INODE_MAX_COUNT; i++) {//写入65535个i节点的信息
        if (!inode_dump(&dev_inode_ctrl, i, &inode)) {//把i节点的数据结构写入超级块 i.mode=0
            log_debug("inode写入失败: %d号设备, %d号inode", device, (int)i);//写入信息失败
            return false;
        }
    }

    /* 初始化磁盘的data block区 */
    block_no_t data_block_free_stack;//存放空簇的栈
    bool success = data_blocks_init(device, sectors_per_block, data_block, block_count - data_block, &data_block_free_stack);
    //设备 每簇扇区（1） 数据区开始 数据区簇数 数据区开始
    if (!success) {
        log_debug("data_block区初始化失败: %d号设备", device);
        return false;
    }

    /* 建立根目录 */
    superblock_t temp_sb;//超级块建立
    superblock_create(&temp_sb, device_section_count(device), sectors_per_block,
                      inode_table, INODE_MAX_COUNT, data_block, 
                      data_block_free_stack, 0);
    //数据块数 每簇扇区数
    //inode_table 最大inode数(63553?) 数据块 超级块（栈） 
    /*i节点数目待优化*/
    //根节点位置 0
    inode_no_t root_dir;
    success = base_file_create(device, &temp_sb, MODE_DIR, &root_dir);
    //设备 超级块 enum根目录(2) 返回值root_dir
    if (!success) {
        return false;
    }



    /* 写入superblock */
    superblock_t sb;
    superblock_create(&sb, device_section_count(device), sectors_per_block,
                      inode_table, INODE_MAX_COUNT, data_block,
                       data_block_free_stack, root_dir);
    //写入超级块 设备扇区数 簇所占扇区数 放inode的
    if (!superblock_dump(device, &sb)) {//写入超级块sb  以及设备句柄
        log_debug("superblock写入失败: %d号设备", device);
        return false;
    }

    return true;
}

bool fulfs_filesystem_init(fulfs_filesystem_t* fs, device_handle_t device)
{
    return superblock_load(device, &fs->sb);
}

fulfs_filesystem_t* fulfs_filesystem_new(device_handle_t device)
{
    fulfs_filesystem_t* fs_ctrl = FT_NEW(fulfs_filesystem_t, 1);
    if (fs_ctrl == NULL) {
        return false;
    }

    if (!fulfs_filesystem_init(fs_ctrl, device)) {
        return false;
    }

    return fs_ctrl;
}

fs_size_t fulfs_filesystem_used_size(fulfs_filesystem_t* fs)
{
    return superblock_used_size(&fs->sb);
}

fs_size_t fulfs_filesystem_total_size(fulfs_filesystem_t* fs)
{
    return superblock_total_size(&fs->sb);
}
