#include "mem_inode.h"

#include <stdbool.h>
#include "inode.h"

#include <assert.h>

/* 目前采用简单的数组保存和组织内存inode节点
   这会造成一些性能问题，后期应该重构成hash或平衡树来提高性能
*/

static struct {
    mem_inode_t mem_inode;
    //内存i节点编号
    //可能多个指向一个
    bool is_free;//状态位 是否为空
}mem_inodes[MAX_MEM_INODE_COUNT];
//最大的内存节点
static bool mem_inodes_inited = false;//内存节点初始化标志


#define READ_ERROR -1
static int mem_inode_read(dev_inode_ctrl_t* dev_inode_ctrl, inode_no_t inode_no);
static void mem_inodes_init();

bool mem_inode_get(dev_inode_ctrl_t* dev_inode_ctrl, inode_no_t inode_no, mem_inode_t** p_result)
//返回数组 因为可能不止一个节点储存
//获取所有空闲节点
{//获得存储数据的memory节点 //返回数组
    if (!mem_inodes_inited) {//开机时对mem_node初始化
        mem_inodes_init();
        mem_inodes_inited = true;
    }

 /*   for (int i = 0; i < MAX_MEM_INODE_COUNT; i++) {//将inode读取到inode的缓冲区
        mem_inode_t* mem_inode = &(mem_inodes[i].mem_inode);
        if (!mem_inodes[i].is_free &&
            mem_inode->device == dev_inode_ctrl->device && mem_inode->inode_no == inode_no) {
            *p_result = mem_inode;//如果找到一个mem节点则返回结果
        }
    }
*/
    //如果缓冲区没有结果
    int i = mem_inode_read(dev_inode_ctrl, inode_no);//从设备中读取mem_inode
    //则从文件中加载
    if (i != READ_ERROR) {
        //结果为初始化的mem_inodes
        *p_result = &(mem_inodes[i].mem_inode);
    } else {
        return false;
    }

    (*p_result)->ref_count++;
    return true;
}

bool mem_inode_put(dev_inode_ctrl_t* dev_inode_ctrl, mem_inode_t* mem_inode)
{
    mem_inode->ref_count--;//引用减一

    if (mem_inode->ref_count <= 0) {
        bool success = inode_dump(dev_inode_ctrl, mem_inode->inode_no, &(mem_inode->inode));
        if (!success) {
            return false;
        }

        for (int i = 0; i < MAX_MEM_INODE_COUNT; i++) {
            if (&(mem_inodes[i].mem_inode) == mem_inode) {
                mem_inodes[i].is_free = true;
                return true;
            }
        }

        assert(false);

    } else {
        return true;
    }
}

static int mem_inode_read(dev_inode_ctrl_t* dev_inode_ctrl, inode_no_t inode_no)
{
    for (int i = 0; i < MAX_MEM_INODE_COUNT; i++) {
        if (mem_inodes[i].is_free) {//当前块空闲
            bool success = inode_load(dev_inode_ctrl, inode_no, &(mem_inodes[i].mem_inode.inode));
            if (!success) {//成功后分配出来
                return READ_ERROR;
            }

            mem_inodes[i].is_free = false;//置状态位为占用
            mem_inodes[i].mem_inode.inode_no = inode_no;
            mem_inodes[i].mem_inode.ref_count = 0;//初始化引用0
            mem_inodes[i].mem_inode.device = dev_inode_ctrl->device;
            return i;//返回存放的i节点
        }
    }

    /* 超过了MAX_MEM_INODE_COUNT */
    return READ_ERROR;
}

static void mem_inodes_init()//所有内存节点置空
{
    for (int i = 0; i < MAX_MEM_INODE_COUNT; i++) {
        mem_inodes[i].is_free = true;
    }
}
