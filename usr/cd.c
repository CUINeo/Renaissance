#include <driver/vga.h>
#include <zjunix/fs/fat.h>
#include "cd.h"
#include "ls.h"
int str_equal(char *dir, char *cmp, int len)
{
    int i;
    for (i = 0; i < len; i++)
    {
        if (dir[i] != cmp[i])
        {
            return 0;
        }
    }
    return 1;
}

int str_len(char *str)
{
    int length = 0;
    int i;
    for (i = 0; str[i] != 0; i++)
    {
        length++;
    }
    return length;
}

int combine(char *dest, char *str1, char *str2, char append)
{
    int i, j;
    for (i = 0; i < str_len(str1) && str1[i] != 0; i++)
    {
        dest[i] = str1[i];
    }
    dest[i] = 0;
    if (append != 0)
    {
        dest[i] = append;
        i++;
    }

    for (j = 0; str2[j] != 0; j++)
    {
        dest[i + j] = str2[j];
    }
    dest[i + j] = 0;
    return i + j;
}

int cd(char *para, char *pwd)
{
    char dir[32];
    char tmp[128];
    unsigned int index;
    FS_FAT_DIR f_dir;

    para = cut_front_blank(para);
    each_param(para, dir, 0, ' ');

    if (str_equal(dir, ".", 2))
    {
        return 0;
    }

    if (str_equal(dir, "..", 3))
    {
        if (str_equal(pwd, "/", 2))
        {
            return 0;
        }//if current dir is root do nothing
        else
        {
            index = str_len(pwd) - 1;
            //clear current path
            while (pwd[index] != '/')
            {
                pwd[index--] = 0;
            }
            if (index)
            {
                pwd[index] = 0;
            }
            return 0;
        }
    }

    if (dir[0] == '/')
    {
        if (dir[str_len(dir) - 1] == '/')
        {
            dir[str_len(dir) - 1] = 0;
        }
        kernel_memcpy(tmp, dir, str_len(dir) + 1);
    check_dir:
    //check if the dir is existing
        if (1 == fs_open_dir(&f_dir, tmp))
        {
            kernel_printf("No such directory : %s\n", tmp);
            return 1;
        }
        kernel_memcpy(pwd, tmp, str_len(tmp) + 1);
        return 0;
    }

    if (pwd[str_len(pwd) - 1] == '/')
    {
        combine(tmp, pwd, dir, 0);
    }
    else
    {
        combine(tmp, pwd, dir, '/');
    }

    goto check_dir;
}