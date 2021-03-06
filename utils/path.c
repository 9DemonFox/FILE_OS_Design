#include "path.h"

#include "../memory/alloc.h"
#include<string.h>
#include<stdbool.h>

#include <assert.h>

static char* str_next(char* s, char c);
static bool str_equal(const char* start, size_t size, const char* s2);

void path_simplify(char* path)
{//路径化简 即是去除 /
    int size = strlen(path);

    char* buf = FT_NEW(char, size);
    char* top = buf;

    char* start = path;
    char* end =  str_next(path, '/');

    while (*start != '\0') {
        if (!str_equal(start, end - start, "/.")) {
            //比较有限字符数
            if (!str_equal(start, end - start, "/..")) {
                memcpy(top, start, end - start);
                top += end - start;
                *top = '\0';
            } else {
                top = (char *)path_p_basename(buf) - 1;
                *top = '\0';
            }
        }

        start = end;
        end = str_next(end, '/');
    }

    strcpy(path, buf);
    ft_free(buf);
}

static char* str_next(char* s, char c)
{//传入path ‘/’
    if (*s == c) {
        s++;
    }//如果遇见'/'地址右移

    while (*s != '\0') {
        if (*s == c) {
            return s;
        }
        s++;
    }
    return s;
}

static bool str_equal(const char* start, size_t size, const char* s2)
{
    if (strlen(s2) != size) {
        return false;
    }

    for (size_t i = 0; i < size; i++) {
        if (start[i] != s2[i]) {
            return false;
        }
    }
    return true;
}

void path_dirname(const char* path, char* dir)
{
    strcpy(dir, path);//后者复制到前者
    int src_size = strlen(path);//path长度
    bool is_abs = (dir[0] == '/');
    for (int i = src_size - 1; i >= 0; i--) {//从后往前扫描
        if (dir[i] == '/') {
            /* 绝对路径的情况下，/的上级还是/，/xxx 的上级是/ */
            if (is_abs && i == 0) {
                dir[i + 1] = '\0';
                return;//返回 ’\‘
            } else {//否则把斜杠删除
                dir[i] = '\0';
                return;
            }
        }
    }

    /* 能走到这儿的肯定是相对路径 */
    dir[0] = '\0';//相对路径前边没有’/‘
}

void path_basename(const char* path, char* name, size_t size)
{
    const char* p_name = path_p_basename(path);
    strncpy(name, p_name, size);
    //p开始size个字节复制到那么
    //赋复制文件名
    name[size] = '\0';
}

const char* path_p_basename(const char* path)
{
    int src_size = strlen(path);
    if (src_size == 0) {
        return path;
    }

    for (int i = src_size - 1; i >= 0; i--) {
        if (path[i] == '/') {
            return path + i + 1;//返回本级文件名
        }
    }
    return path;//如果在根目录下，直接返回文件名
}

char* path_join(char* path1, size_t size, const char* path)
//链接路径
{
    size_t top = 0;//循环寻找顶部
    while (path1[top] != '\0') {
        top++;
    }

    if (top > 0  && path1[top - 1] != '/') {
        if (top >= size) {
            return path1;
        }

        path1[top] = '/';
        top++;
        path1[top + 1] = '\0';//结束
    }

    strncpy(path1 + top, path, size - top - 1);
    (path1 + top)[size - top - 1] = '\0';
    return path1;
}
