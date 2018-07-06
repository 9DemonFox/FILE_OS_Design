#include "superblock.h"

#include <assert.h>
#include <string.h>


size_t superblock_bin_size(void)
{
    assert(sizeof(superblock_t) <= 512);
//断言超级块小于512，即一个扇区大小
    return sizeof(superblock_t);
//返回超级块的大小
}

/* NOTE:目前的实现并未考虑太多的通用性，包括内存布局和字节序 */
void superblock_dump_to_bin(superblock_t* sb, char* bin, size_t bin_size)
//sb超级块 
{
    assert(bin_size >= superblock_bin_size());

    *(superblock_t *)bin = *sb;
    memset(bin + superblock_bin_size(), 0, bin_size - superblock_bin_size());
    //补充字符0
}

void superblock_load_from_bin(const char* bin, superblock_t* sb)
{
    *sb = *(superblock_t *)bin;
}

void superblock_create(superblock_t* sb, sector_no_t sectors, int sectors_per_block,
                       block_no_t inode_table, inode_no_t inode_count, 
                       block_no_t data_block, block_no_t data_block_free_stack, inode_no_t root_inode)
{
    sb->sectors = sectors;
    sb->sectors_per_block = sectors_per_block;

    sb->total_size = ((sb->sectors / sb->sectors_per_block) - data_block) * (BYTES_PER_SECTOR * sb->sectors_per_block);
    /* sb->used_size = 0; */

    sb->root_dir = root_inode;//指向根目录的i节点
    sb->inode_table_block = inode_table;//指向inode table的起始block号
    sb->data_block = data_block;//data区的起始block号
    sb->data_block_free_stack = data_block_free_stack;//空闲管理的超级块（栈）


    sb->used_inode_count = 1;//已经使用的inode数目
    sb->used_data_block_count = 0;//已经使用的数据块
    sb->inode_total_count = inode_count;//总的inode数
}


bool superblock_load(device_handle_t device, superblock_t* sb)
{
    char buf[512];
    bool success = DEVICE_IO_SUCCESS(device_read(device, 0, 1, buf));
    if (!success) {
        return false;
    }
    superblock_load_from_bin(buf, sb);

    return true;
}

bool superblock_dump(device_handle_t device, superblock_t* sb)
//设备号，超级块
{
    char buf[512];
    superblock_dump_to_bin(sb, buf, sizeof(buf));
    //超级块 缓冲区 大小 
    //变为合适的大小
    return DEVICE_IO_SUCCESS(device_write(device, 0, 1, buf));
}


block_no_t superblock_block_count(const superblock_t* sb)
{
    return sb->sectors / sb->sectors_per_block;
}

uint64_t superblock_total_size(const superblock_t* sb)
{
    return sb->total_size;
}
uint64_t superblock_used_size(const superblock_t* sb)
{
    fsize_t inode_table_total_size = superblock_inode_table_blocksize(sb) * superblock_block_size(sb);

    /* 按比例计算 */
    /* sb->used_inode_count / sb->inode_total_count * inode_table_total_size */
    fsize_t inode_used_size = sb->used_inode_count  * inode_table_total_size / sb->inode_total_count;
    fsize_t data_block_used_size = sb->used_data_block_count * superblock_block_size(sb);

    return inode_used_size + data_block_used_size;
}

uint64_t superblock_free_size(const superblock_t* sb)
{
    uint64_t used_size = superblock_used_size(sb);
    uint64_t total_size = superblock_total_size(sb);

    assert(used_size <= total_size);

    return total_size - used_size;
}

void superblock_set_used_size(superblock_t* sb, uint64_t new_size)
{
    //程序运行至此则退出
    assert(false);
    /* assert(sb->used_size + new_size <= sb->total_size); */

    /* sb->used_size = new_size; */

}

inode_no_t superblock_root_dir_inode(const superblock_t* sb)
{
    return sb->root_dir;//超级块到根目录节点
}


block_no_t superblock_inode_table_start(const superblock_t* sb)
{
    return sb->inode_table_block;
}

block_no_t superblock_inode_table_blocksize(const superblock_t* sb)
{
    assert(sb->data_block > sb->inode_table_block);

    return sb->data_block - sb->inode_table_block;
}

block_no_t superblock_data_block_start(const superblock_t* sb)
{
    return sb->data_block;
}

block_no_t superblock_data_block_size(const superblock_t* sb)
{
    assert(superblock_block_count(sb) > sb->data_block);

    return superblock_block_count(sb) - sb->data_block;
}

block_no_t superblock_data_block_free_stack(const superblock_t* sb)
{
    return sb->data_block_free_stack;
}

void superblock_data_block_free_stack_set(superblock_t* sb, block_no_t new_stack)
{
    sb->data_block_free_stack = new_stack;
}

size_t superblock_block_size(const superblock_t* sb)
{
    return sb->sectors_per_block * BYTES_PER_SECTOR;
}

int superblock_sectors_per_block(const superblock_t *sb)
{
    return sb->sectors_per_block;
}
