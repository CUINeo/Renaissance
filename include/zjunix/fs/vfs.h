#ifndef _ZJUNIX_FS_VFS_H
#define _ZJUNIX_FS_VFS_H

#include <zjunix/fs/fat.h>
#include <zjunix/type.h>

struct fat32 
{
	struct FILE *file;
	//创建文件
	u32 (*create)(unsigned char *filename);
	//删除文件
	u32 (*rm)(unsigned char *filename);
	//创建文件夹
	u32 (*mkdir)(unsigned char *filename);
	//删除文件夹
	u32 (*rmdir)(unsigned char *filename);
	//打开文件
	u32 (*open)(FILE *file, unsigned char *filename);
	//关闭文件
	u32 (*close)(FILE *file);
	//读文件
	u32 (*read)(FILE *file, unsigned char *buf, unsigned long count);
	//写文件
	u32 (*write)(FILE *file, const unsigned char *buf, unsigned long count);
	//flush
	u32 (*fflush)();
	//重定位
	void (*lseek)(FILE *file, unsigned long new_loc);
	//找文件
	u32 (*find)(FILE *file);
	//文件移动
	u32 (*mv)(unsigned char *src, unsigned char *dest);
	//文件拷贝
	u32 (*cp)(unsigned char *src, unsigned char *dest);
	//cat 操作
	u32 (*cat)(unsigned char *path);
	//打开文件夹
	u32 (*open_dir)(FS_FAT_DIR *dir, unsigned char *filename);
	//读取文件夹
	u32 (*read_dir)(FS_FAT_DIR *dir, unsigned char *buf);
	//create new file
	u32 (*touch)(unsigned char * path);
	//初始化
	u32 (*init)();
};

/*struct ext2 {
	//初始化
	u32 (*init)();
	//找文件
	u32 (*find)();
	//打开文件
	u32 (*open)();
	//关闭文件
	u32 (*close)();
	//读取文件
	u32 (*read)();
	//写文件
	u32 (*write)();
	//flush操作
	u32 (*fflush)();
	//重定向
	void (*lseek)();
};*/

struct vfs
{
	u8 attr; 					//属性位
	u8 ctime_cs;				//创建时间 centiseconds
	u16 ctime;					//创建时间
	u16 cdate;					//创建日期
	u16 adate;					//最近访问时间
	u16 time;					//最近修改时间
	u16 date;					//最近修改日期
	u32 size;					//文件大小
	u32 count;					//文件被引用次数
	//u32 uid;					//用户id
	//u32 gid；					//组id
	struct fat32 * fat32_file;	//fat32 文件操作
	//struct ext2 * ext2_file;	//ext2 文件操作
};

struct vfs* vfsfile;
void initial_vfs();

#endif //!_ZJUNIX_VFS_H
