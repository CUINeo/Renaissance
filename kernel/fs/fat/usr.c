#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include "utils.h"
#include "fat.h"
#include "../../usr/cd.h"

FILE file_create;
u8 mk_dir_buf[32];

//删除文件
u32 fs_rm(u8 *filename) {
	u32 current;
	u32 next;
	FILE mk_dir;

	//如果打开文件失败，报错
	if(fs_open(&mk_dir, filename) == 1)
	{
		goto fs_rm_err;
	}
	kernel_printf("RM has open the src file\n");
	//修改mark值
	mk_dir.entry.data[0] = 0xE5;
	//释放申请的所有块
	current = get_start_cluster(&mk_dir);
	next = current;

	for(; current != 0 && current <= fat_info.total_data_clusters + 1; current = next) {
		if (get_fat_entry_value(current, &next) == 1)
			goto fs_rm_err;
		if(fs_modify_fat(current, 0) == 1)
			goto fs_rm_err;
	}

	if(fs_close(&mk_dir) == 1)
		goto fs_rm_err;
	return 0;
fs_rm_err:
	kernel_printf("Rm failed\n");
	return 1;

}

//删除目录
u32 fs_rmdir(u8 *filename)
{
	u32 current;
	u32 next;
	FILE rm_dir;

	if(fs_open(&rm_dir, filename) == 1)
	{
		goto fs_rmdir_err;
	}

	rm_dir.entry.data[0] = 0xE5;
	current = get_start_cluster(&rm_dir);
	next = current;
	for(; current != 0 && current <= fat_info.total_data_clusters + 1; current = next)
	{
		if(get_fat_entry_value(current, &next) == 1)
			goto fs_rmdir_err;
		//修改fat表实现释放
		if(fs_modify_fat(current, 0) == 1)
			goto fs_rmdir_err;
	}
	if(fs_close(&rm_dir) == 1)
		goto fs_rmdir_err;

	return 0;
fs_rmdir_err:
	return 1;
}

//实现文件的移动
/*u32 fs_mv(u8 *src, u8 *dest)
{
	u32 k;
	FILE mv_dir;
	u8 filename[13];

	//判断源文件是否存在
	if(fs_open(&mv_dir, src) == 1)
		goto fs_mv_err;
	//如果源文件存在，则在新位置创建文件
	if(fs_create_with_attr(dest, mv_dir.entry.data[11]) == 1)
		goto fs_mv_err;
	//将目录项放到buf中
	for (k = 0; k < 32; k++)
	{
		mk_dir_buf[k] = mv_dir.entry.data[k];
	}
	if(fs_open(&file_create, dest) == 1)
		goto fs_mv_err;
	for (k = 0; k < 32; k++)
	{
		file_create.entry.data[k] = mk_dir_buf[k];
	}
	if(fs_close(&file_create) == 1)
		goto fs_mv_err;
	mv_dir.entry.data[0] = 0xE5;
	if(fs_close(&mv_dir) == 1)
		goto fs_mv_err;
	return 0;
fs_mv_err:
	return 1;
}*/

u32 fs_mv(u8 *src, u8 *dest)
{
	kernel_printf("src:%s\n", src);
	kernel_printf("dest:%s\n", dest);

	if (str_len(src) == str_len(dest))
	{
		kernel_printf("src == equal\n");
		if (str_equal(src, dest, str_len(dest)) == 1)
		{
			kernel_printf("Error\n");
			return 1;
		}
	} 
	else {
		kernel_printf("Begin copy\n");
		if(fs_cp(src, dest) == 1)
		{
			kernel_printf ("Error.\n");
			return 1;
		}
		kernel_printf("Finish copy\n");
		kernel_printf("Begin rm file src %s\n", src);
		if (fs_rm(src) == 1)
		{
			kernel_printf("Error\n");
			return 1;
		}
		kernel_printf("Rm Finish\n");
	}
	return 0;
}

//文件复制
u32 fs_cp(u8 *src, u8 *dest)
{
	u32 k;
	FILE cp_dir;

	if(fs_open(&cp_dir, src) == 1)
		goto fs_cp_err;

	if(fs_create_with_attr(dest, cp_dir.entry.data[11]) == 1)
		goto fs_cp_err;

	for (k = 0; k < 32; k++)
		mk_dir_buf[k] = cp_dir.entry.data[k];
	if(fs_open(&file_create, dest) == 1)
	{
		goto fs_cp_err;
	}
	u32 file_size = get_entry_filesize(cp_dir.entry.data);
	u8 *buf = (u8 *)kmalloc(file_size + 1);
	fs_read(&cp_dir, buf, file_size);
	for(k=0; k < 11; k++)
	{
		file_create.entry.data[k] = mk_dir_buf[k];
	}
	fs_write(&file_create, buf, file_size);
	if(fs_close(&file_create) == 1)
		goto fs_cp_err;

	if(fs_close(&cp_dir) == 1)
		goto fs_cp_err;
	return 0;
fs_cp_err:
	return 1;
}

//文件cat操作
u32 fs_cat(u8 *path)
{
	FILE cat_file;

	if(fs_open(&cat_file, path) != 0)
	{
		goto fs_cat_err;
	}
	u32 size = get_entry_filesize(cat_file.entry.data);
	kernel_printf("SIZE: %d\n", size);
	u8 *buf = (u8 *)kmalloc(size + 1);
	fs_read(&cat_file, buf, size);
	//字符串最后一位置为0
	buf[size] = 0;
	kernel_printf(" %s\n", buf);
	fs_close(&cat_file);
	kfree(buf);
	return 0;
fs_cat_err:
	return 1;
}

//创建新文件，文件夹
u32 fs_mkdir(u8 * filename)
{
	FILE mk_dir;
	FILE file_create;
	u32 k;

	if (fs_create_with_attr(filename, 0x10) == 1)
		goto fs_mkdir_err;
	if(fs_open(&mk_dir, filename) == 1)
		goto fs_mkdir_err;
	mk_dir_buf[0] = '.';
	for (k = 0; k < 11; k++)
	{
		mk_dir_buf[k] = 0x20;
	}
	mk_dir_buf[11] = 0x10;
	for (k = 12; k < 32; k++)
	{
		mk_dir_buf[k] = 0;
	}
	if(fs_write(&mk_dir, mk_dir_buf, 32) == 1)
		goto fs_mkdir_err;
	fs_lseek(&mk_dir, 0);
// 	struct __attribute__((__packed__)) dir_entry_attr {
// 	u8 name[8];					    /* Name 8 char*/
// 	u8 ext[3];					    /* Extension*/
// 	u8 attr;						/* attribute bits */
// 	u8 lcase;						/* Case for base and extension */
// 	u8 ctime_cs;					/* creation time, centiseconds (0~199) */
// 	u16 ctime;						 Creation time 
// 	u16 cdate;						/* Creation data */
// 	u16 starthi;					/* start cluster (Hight 16 bits) */
// 	u16 time; 						/* last modify time */
// 	u16 date;						/* last modify date */
// 	u16 startlow;					/* start cluster (Low 16 bits) */
// 	u32 size;						/* file size (in bytes) */
// };

	mk_dir_buf[20] = mk_dir.entry.data[20];
	mk_dir_buf[21] = mk_dir.entry.data[21];
	mk_dir_buf[26] = mk_dir.entry.data[26];
	mk_dir_buf[27] = mk_dir.entry.data[27];

	if (fs_write(&mk_dir, mk_dir_buf, 32) == 1)
		goto fs_mkdir_err;

	mk_dir_buf[0] = '.';
	mk_dir_buf[1] = '.';

	for (k = 2; k < 11; k++)
		mk_dir_buf[k] = 0x20;
	mk_dir_buf[11] = 0x10;
	for (k = 12; k < 32; k++)
		mk_dir_buf[k] = 0;
	set_u16(mk_dir_buf + 20, (file_create.dir_entry_pos >> 16) & 0xFFFF);
	set_u16(mk_dir_buf + 26, file_create.dir_entry_pos & 0xFFFF);

	if(fs_write(&mk_dir, mk_dir_buf, 32) == 1)
		goto fs_mkdir_err;
	for (k = 28; k < 32; k++)
	{
		mk_dir.entry.data[k] = 0;
	}
	if(fs_close(&mk_dir) == 1)
		goto fs_mkdir_err;
	return 0;
fs_mkdir_err:
	return 1;
}

//创建链接
u32 fs_ln(u8 *src, u8 *filename)
{
	u32 i;
	FILE mk_dir;

	if (fs_open(&mk_dir, src) == 1)
		goto fs_ln_err;
	if(fs_create_with_attr(filename, mk_dir.entry.data[11]) == 1)
		goto fs_ln_err;
	for (i = 0; i < 32; i++)
        mk_dir_buf[i] = mk_dir.entry.data[i];
    if(fs_open(&file_create, filename) == 1)
    	goto fs_ln_err;
    for (i = 0; i < 32; i++)
    {
    	file_create.entry.data[i] = mk_dir_buf[i];
    }
    if(fs_close(&file_create) == 1)
    	goto fs_ln_err;
    if(fs_close(&mk_dir) == 1)
    	goto fs_ln_err;

    return 0;
fs_ln_err:
	return 1;
}

u32 fs_touch(u8 *path)
{
	FILE create_file;
	if(1 == fs_create_with_attr(path, 0x20))
	{
		kernel_printf("Create Failed\n");
		goto fs_touch_err;
	}
	//open the create file
	//kernel_printf("After create file\n");
	if (1 == fs_open(&create_file, path))
		goto fs_touch_err;

	//kernel_printf("After onpen file\n");
	//alloc new cluster for the file
	u32 cluster = get_start_cluster(&create_file);

	//kernel_printf("After find cluster\n");
	if (0 == cluster)
	{
		if (1 == fs_alloc(&cluster))
			goto fs_touch_err;

		create_file.entry.attr.startlow = (u16)(cluster & 0xFFFF);
		create_file.entry.attr.starthi = (u16)((cluster >> 16) & 0xFFFF);
	}
	//the file's size is zero
	create_file.entry.attr.size = 0;
	kernel_printf("Create file.size\n");
	if (1 == fs_close(&create_file))
		goto fs_touch_err;

	return 0;

fs_touch_err:
	return 1;
}