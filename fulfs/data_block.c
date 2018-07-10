
#include "data_block.h"

#include <stdint.h>
#include <stdbool.h>
#include "block.h"

#include <assert.h>

/* 使用成组链接法管理空闲的block */

#define MAX_GROUP_COUNT 100//每个块最大空闲块数

struct group_s{
    uint8_t top;
    block_no_t free_block[MAX_GROUP_COUNT];
};

#define GROUP_NEXT_END ((block_no_t)0)

#define GROUP_NEXT(group) (group.free_block[0])
//定义指向下一块的数据块为block[0]

/* 同理，一个简单的实现，没有考虑字节序和内存布局 */
static void group_load(const char* buf, struct group_s* group) {
    *group = *((struct group_s*)buf);//分配一个组大小的buf
}
static void group_dump(const struct group_s* group, char* buf) {
    *((struct group_s*)buf) = *group;
}


bool data_blocks_init(device_handle_t device, int sectors_per_block, block_no_t data_block_start, block_no_t data_block_count, block_no_t* p_start)
{//设备 每簇扇区（1） 数据区开始 数据区簇数 数据区开始 存放空簇的栈
    block_no_t next = data_block_start;//数据区开始
    for (block_no_t block = data_block_start + 1; block < data_block_start + data_block_count; block += MAX_GROUP_COUNT) {
        //循环条件:直到所有块都分配
        //每次循环+组最大块
        bool is_end = (data_block_start + data_block_count - block) <= MAX_GROUP_COUNT;
        //数据块总数-当前计数小于等于每组的数，即使到达结尾
        struct group_s group;//建立组
        group.top =  is_end ? (data_block_start + data_block_count - block) : MAX_GROUP_COUNT;
        //如果结尾的话就是实际数目，否则是100
        assert(group.top <= MAX_GROUP_COUNT);
        //断言每组的计数块小于100
        for (int i = 0; i < group.top; i++) {
            group.free_block[i] = block + i;
        }//把数据块编号写入数组

        if (is_end) {
            GROUP_NEXT(group) = GROUP_NEXT_END;
        }//最后一组的计数标志位为0

        char buf[MAX_BYTES_PER_BLOCK];
        //一个块大小的buf
        group_dump(&group, buf);
        //数组写入缓冲区
        assert(next >= data_block_start);
        //断言next比数据区大
        bool success = block_write(device, sectors_per_block, next, buf);//写入块
        if (!success) {
            return false;
        }
        next = GROUP_NEXT(group);
    }//while
    *p_start = data_block_start;
    return true;
    //成组链接分配法
}

bool data_block_alloc(device_handle_t device, int sectors_per_block, block_no_t data_blocks_stack, block_no_t* p_block, block_no_t* p_used_block_count)
{//成组链接分配
    char buf[MAX_BYTES_PER_BLOCK];
    block_read(device, sectors_per_block, data_blocks_stack, buf);

    struct group_s group;
    group_load(buf, &group);

    assert(group.top > 0);//断言超级块栈顶大于0
    assert(group.top <= MAX_GROUP_COUNT);


    if (group.top == 1) {//如果栈顶（当前超级块还有一个索引块）
        block_no_t next = GROUP_NEXT(group);//栈顶指向下一块
        if (next == GROUP_NEXT_END) {//如果标志位为0，分配失败
            return false;
        }

        *p_block = group.free_block[--group.top];//讲当前块分出

        bool success = block_copy(device, sectors_per_block, next, data_blocks_stack);//把超级快的内容拷贝到下一个块
        if (!success) {
            return false;
        }
    } else {
        *p_block = group.free_block[--group.top];//如果是平常情况，那么顺序分出一块

        group_dump(&group, buf);
        bool success = block_write(device, sectors_per_block, data_blocks_stack, buf);
        if (!success) {
            return false;
        }
    }
    *p_used_block_count += 1;
    //已经使用的块数+1
    return true;
}

bool data_block_free(device_handle_t device, int sectors_per_block, block_no_t data_blocks_stack, block_no_t block, block_no_t* p_used_block_count)
{//空闲块块的回收
    char buf[MAX_BYTES_PER_BLOCK];
    block_read(device, sectors_per_block, data_blocks_stack, buf);
    //读出一簇的内容
    struct group_s group;
    group_load(buf, &group);
    //读出组的内容
    assert(group.top <= MAX_GROUP_COUNT);

    if (group.top == MAX_GROUP_COUNT) {
        bool success = block_copy(device, sectors_per_block, data_blocks_stack, block);
        if (!success) {
            return false;
        }
        group.top = 1;
        GROUP_NEXT(group) = block;
    } else {
        group.free_block[group.top++] = block;//先释放block，然后组内计数加1
    }

    group_dump(&group, buf);//写会
    bool success = block_write(device, sectors_per_block, data_blocks_stack, buf);//写会datastack
    if (!success) {
        return false;
    }

    assert(p_used_block_count > 0);

    *p_used_block_count -= 1;
    return true;
}

bool data_block_print(device_handle_t device,int sectors_per_block,
                    block_no_t data_block_begin,block_no_t data_blocks_stack)
{//考虑空间问题，只显示前两个块
//传入初始化的节点
//成组链接分配情况
//重要参数，未使用的数据块数目，每组多少块
    char buf[MAX_BYTES_PER_BLOCK];
    block_read(device,sectors_per_block,data_blocks_stack,buf);
    struct group_s group;
    group_load(buf,&group);//加载出了group
    printf("超级块空闲块计数：%d",group.top);
    printf("\n");
    printf("下一组块号:%d\n",GROUP_NEXT(group));
    printf("空闲块号:\n");
    for(int i=1;i<group.top;i++){
        printf("%d\t",group.free_block[i]);
        if(i%10==0)printf("\n");
    }
    printf("\n");
    return true;
}
