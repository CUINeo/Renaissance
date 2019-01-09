#include <zjunix/slab.h>
#include "vfs.h"
#include <zjunix/fs/fat.h>
extern struct vfs* vfsfile;

//vfs初始化的过程，就是将虚拟文件系统操作与实际文件系统绑定的过程。
void initial_vfs()
{
	//申请相应空间
	vfsfile = (struct vfs*)kmalloc(sizeof(struct vfs));
	vfsfile->fat32_file = (struct fat32*)kmalloc(sizeof(struct fat32));
	//vfsfile->ext2_file = (struct ext2*)kmalloc(sizeof(struct ext2));

	vfsfile->fat32_file->create = &fs_create; 		//绑定创建文件函数
	vfsfile->fat32_file->rm = &fs_rm;				//绑定删除文件函数
	vfsfile->fat32_file->mkdir = &fs_mkdir;			//绑定创建文件夹函数
	vfsfile->fat32_file->rmdir = &fs_rmdir;			//绑定删除文件夹函数
	vfsfile->fat32_file->open = &fs_open;			//绑定打开文件函数
	vfsfile->fat32_file->close = &fs_close;			//绑定关闭文件函数
	vfsfile->fat32_file->read = &fs_read;			//绑定读文件函数
	vfsfile->fat32_file->write = &fs_write;			//绑定写文件函数
	vfsfile->fat32_file->fflush = &fs_fflush;		//绑定fflush函数
	vfsfile->fat32_file->lseek = &fs_lseek;			//绑定重定位函数
	vfsfile->fat32_file->find = &fs_find;			//绑定找文件函数
	vfsfile->fat32_file->mv = &fs_mv;				//绑定移动文件函数
	vfsfile->fat32_file->cp = &fs_cp;				//绑定拷贝文件函数
	vfsfile->fat32_file->cat = &fs_cat;				//绑定cat函数操作
	vfsfile->fat32_file->open_dir = &fs_open_dir;	//绑定打开文件夹操作
	vfsfile->fat32_file->read_dir = &fs_read_dir;	//绑定读文件夹操作
	vfsfile->fat32_file->init = &init_fs;			//绑定初始化文件系统操作
}
