#include "shell_command.h"

#include <string.h>
#include "utils/path.h"
#include "utils/sys.h"
#include "fulfs/superblock.h"
#include  "fulfs/block.h"
/*
int cmd_pwd(int argc, char* argv[])
{
    char path[FS_MAX_FILE_PATH];
    fs_getcwd(path, FS_MAX_FILE_PATH);
    printf("%s\n", path);
    return 0;
}

int cmd_cd(int argc, char* argv[])
{
    if (argc <= 0) {
        printf("命令有误\n");
        return -1;
    }

    if (fs_chdir(argv[0]) == FS_SUCCESS) {
        return 0;
    } else {
        printf("改变失败，可能目录不存在\n");
        return -1;
    }

}

int cmd_ls(int argc, char* argv[])
{
    const char* path;
    char wd[FS_MAX_FILE_PATH];
    if (argc <= 0) {
        fs_getcwd(wd, FS_MAX_FILE_PATH);
        path = wd;
    } else {
        path = argv[0];
    }

    FS_DIR* dir = fs_opendir(path);
    if (dir == NULL) {
        printf("列出目录信息失败\n");
    }

    char name[32];
    do {
        fs_readdir(dir, name);
        printf("%s ", name);
    } while(name[0] != '\0');
    printf("\n");

    fs_closedir(dir);

    return 0;
}

int cmd_mkdir(int argc, char* argv[])
{
    if (argc <= 0) {
        printf("命令有误\n");
        return -1;
    }

    if (fs_mkdir(argv[0]) == FS_SUCCESS) {
        return 0;
    } else {
        printf("建立失败\n");
        return -1;
    }
}

*/

static bool tree(char* path, int* p_level)
{
    for (int i = 0; i < *p_level; i++) {
        printf("   ");
    }
    printf("%s\n", path_p_basename(path));

    struct fs_stat st;
    fs_stat(path, &st);
    if (st.st_mode != FS_S_IFDIR) {
        return true;
    }

    FS_DIR* dir = fs_opendir(path);
    char name[32];

    while ((fs_readdir(dir, name), name[0] != '\0')) {
        size_t tail = strlen(path);
        path_join(path, FS_MAX_FILE_PATH, name);

        (*p_level)++;
        tree(path, p_level);
        (*p_level)--;
        path[tail] = '\0';
    }

    fs_closedir(dir);
        return true;
}

int cmd_tree(int argc, char* argv[])
{
    char path[FS_MAX_FILE_PATH];
    if (argc <= 0) {
        fs_getcwd(path, FS_MAX_FILE_PATH);
    } else {
        strcpy(path, argv[0]);
    }

    int level = 0;
    tree(path, &level);
    return 0;
}


int cmd_createfile(int argc, char* argv[])
{//命令解析
    if (argc != 2) {
        printf("参数错误！\n");
        return -1;
    }

    int ret = fs_stat(argv[0], NULL);//返回处理结果
    if(ret == FS_SUCCESS) {
        printf("已存在同名文件！\n");
        return -1;
    }

    int fd = fs_open(argv[0]);
    //打开文件时产生文件标识符
    fs_write(fd, argv[1], strlen(argv[1]));
    //传入参数 文件标识符 文件内容 文件长度
    fs_close(fd);

    return 0;
}

int cmd_cat(int argc, char* argv[])
{
    if (argc != 1) {
        printf("参数错误！\n");
        return -1;
    }

    struct fs_stat st;//获取文件系统
    int ret = fs_stat(argv[0], &st);//获取文件信息 st
    if(ret != FS_SUCCESS) {
        printf("文件不存在！\n");
        return -1;
    }

    if (st.st_mode != FS_S_IFREG) {
        printf("不是普通文件！\n");
        return -1;
    }

    char buf[1024];
    int size = 1024;


    int fd = fs_open(argv[0]);
    while (true) {
        int count = fs_read(fd, buf, size - 1);
        buf[count] = '\0';
        printf("%s", buf);
        if (count != size - 1) {
            break;
        }
    }
    printf("\n");
    fs_close(fd);

    return 0;
}

int cmd_df(int argc, char* argv[])
{
    printf("盘号\t设备号\t类型\t已用\t总共\n");
    for (int i = 0; i <= 'z' - 'a'; i++) {
        struct dev_fsctrl_s ctrl;
        if (fs_dev_fs_ctrl(i + 'a', &ctrl)) {
            printf("%c\t", i + 'A');
            printf("%d\t", ctrl.device);

            printf("%s\t", ctrl.fs_type == FS_TYPE_FULFS ? "fulfs" : "未知");
            //暂时支持FS_TYPE_FULFS
            size_t size;
            char sym = ft_human_size((size_t)fs_filesystem_used_size(i + 'a'), &size);
            printf("%d%c\t", (int)size, sym);

            sym = ft_human_size((size_t)fs_filesystem_total_size(i + 'a'), &size);
            printf("%d%c\t", (int)size, sym);
            printf("\n");
            /*加载sb*/
            superblock_t  sb;
            superblock_load(ctrl.device,&sb);//读出sb
            printf("根i\t扇/簇\t起始簇\t已用簇\t已用i\n");
            printf("%d\t",sb.root_dir);
            printf("%d\t",sb.sectors_per_block);
        //    printf("%d\t",sb.data_block_free_stack);//空闲块管理相关
            printf("%d\t",sb.data_block);//起始区的block号；
            printf("%d\t",sb.used_data_block_count);
            printf("%d\t",sb.used_inode_count);
            printf("\n");
            data_block_print(ctrl.device,sb.sectors_per_block,sb.data_block_free_stack,sb.data_block_free_stack);

        }
    }
    return 0;
}


int cmd_pwd(int argc, char* argv[])
//输出当前目录
{
    char path[FS_MAX_FILE_PATH];
    fs_getcwd(path, FS_MAX_FILE_PATH);
    printf("%s\n", path);
    return 0;
}

int cmd_cd(int argc, char* argv[])
{
	if(argc !=1){
	   printf("输入格式错误");
	   return -1;
   }
   int a = fs_chdir(argv[0]);
   if(a == -1){
	   printf("改变目录失败\n");
	   return -1;
   }
    return 0;
}

int cmd_ls(int argc, char* argv[])
{//shell层 解析命令
	if(argc == 0){
		char path[FS_MAX_FILE_PATH];
	    fs_getcwd(path,FS_MAX_FILE_PATH);
        //当前目录带盘符
		FS_DIR* dir  =  fs_opendir(path);
        //打开当前目录
    if (dir == NULL){
        printf("路径名无效\n");
        return -1;
    }
		char name[100] = "no";
        for(int i = 0;name[0] != '\0';i++){
	       fs_readdir(dir, name);//文件夹和n
           printf("%s ",name);
        }
        printf("\n");
		return -1;
	}
	if(argc > 1){
	   printf("输入格式错误\n");
	   return -1;
   }
   FS_DIR* dir  =  fs_opendir(argv[0]);
   if (dir == NULL){
	   printf("路径名无效\n");
	   return -1;
   }//？
   char name[100]="no";
   for(int i = 0;name[0] != '\0';i++){
	  fs_readdir(dir, name);
      printf("%s ",name);
   }
   printf("\n");
    return 0;
}

int cmd_mkdir(int argc, char* argv[])
{
	int a = fs_mkdir(argv[0]);
    //命令层交付文件层处理，将建立文件路径传到文件层
	if(a == -1){
		printf("输入格式错误");
		return -1;
	}
    return 0;
}

int cmd_rmdir(int argc, char* argv[])
{
	struct fs_stat st;
	int ret = fs_stat(argv[0], &st);//传回文件的统计信息
	if(ret == -1){
		printf("目录不存在");
		return -1;
	}

	else if (st.st_mode == FS_S_IFDIR) {
		fs_rmdir(argv[0]);
	}
	else printf("此文件不是目录不能执行删除目录操作");
    return 0;
}

int cmd_stat(int argc, char* argv[])
{//文件信息统计结果
    if (argc != 1) {
        printf("参数错误\n");
        return -1;
    }

	struct fs_stat  st;
	int ret = fs_stat(argv[0], &st);
  if (ret != FS_SUCCESS) {
      printf("此路径可能不存在\n");
      return -1;
  }
	if (st.st_mode == FS_S_IFDIR){
		printf("此文件是目录\n");
	}
	else if (st.st_mode == FS_S_IFLNK){
		printf("此文件为符号链接\n");
	}
	else if (st.st_mode == FS_S_IFREG){
		printf("此文件为普通文件\n");
	}
	printf("inode 编号：  %d\n", (int)st.st_ino);
	printf("硬连接数目：  %d\n", (int)st.st_nlink);
	printf("文件大小：    %d\n", (int)st.st_size);
	printf("块大小：      %d\n", (int)st.st_blksize);
	printf("块数：        %d\n", (int)st.st_blocks);

  char buf[32];
  size_t size = 32;
  strftime(buf, size, "%Y-%m-%d %H:%M:%S", localtime(&st.st_ctime));
	printf("创建时间：            %s\n", buf);

  strftime(buf, size, "%Y-%m-%d %H:%M:%S", localtime(&st.st_atime));
	printf("最后一次访问时间：    %s\n", buf);

  strftime(buf, size, "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));
	printf("最后一次修改时间：    %s\n", buf);
    return 0;
}


int cmd_cp(int argc, char* argv[])
{
    int sourceFile;
    int targetFile;
    if(argc == 2)
    {
        sourceFile = fs_open(argv[0]);
        if (sourceFile == FS_ERROR)
        {
            /* code */
            printf("操作失败！\n");
            return 0;
        }
        targetFile = fs_open(argv[1]);
        if (targetFile == FS_ERROR)
        {
            /* code */
            printf("操作失败！\n");
            return 0;
        }
        char buf[1024];
        int c = fs_read(sourceFile, buf, sizeof(buf) - 1);
        while(c > 0)
        {
            fs_write(targetFile, buf, c);
            c = fs_read(sourceFile, buf, sizeof(buf) - 1);
        }

        fs_close(sourceFile);
        fs_close(targetFile);
    }
    else
    {
        printf("指令格式不正确！\n");
    }
    return 0;
}

int cmd_mv(int argc, char* argv[])
{
    if (argc == 2)
    {
        int isSuccessed;
        isSuccessed = fs_link(argv[0],argv[1]);
        if (isSuccessed == FS_ERROR)
        {
            printf("移动失败！\n");
            return 0;
        }
        fs_unlink(argv[0]);
    }
    else
    {
       printf("指令格式不正确！\n");
    }
    return 0;
}

int cmd_rm(int argc, char* argv[])
{
    int isSuccessed;
    for(int i =0;i<argc;i++)
    {//命令处理
            isSuccessed =  fs_unlink(argv[i]);
            if(isSuccessed == FS_ERROR)
            {
                printf("%s:删除失败！\n",argv[i]);
            }
    }
    return 0;
}

int cmd_ln(int argc, char* argv[])
{
    int isSuccessed;
    if (argc == 2)
    {
        isSuccessed = fs_link(argv[0],argv[1]);
        if (isSuccessed == FS_ERROR)
        {
            /* code */
            printf("操作失败！\n");
        }
    }
    else if (argc == 3)
    {
        /* code */
        if (argv[0][0] == '-' && argv[0][1] == 's' && argv[0][2] == '\0')
        {
            /* code */
            isSuccessed = fs_symlink(argv[1],argv[2]);
            if (isSuccessed == FS_ERROR)
            {
                /* code */
                printf("操作失败！\n");
            }
        }
        else
        {
            printf("指令格式不正确！\n");
        }
    }
    else
    {
        printf("指令格式不正确！\n");
    }
    return 0;
}

