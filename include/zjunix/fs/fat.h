#ifndef _ZJUNIX_FS_FAT_H
#define _ZJUNIX_FS_FAT_H

#include <zjunix/type.h>
#include <zjunix/fs/fscache.h>

/* 4k data buffer number in each file struct */
#define LOCAL_DATA_BUF_NUM 4

/* Sector Size */
#define SECTOR_SIZE 512
/* Cluster Size */
#define CLUSTER_SIZE 4096

/* The attribute of dir entry */
struct __attribute__((__packed__)) dir_entry_attr {
	u8 name[8];					    /* Name 8 char*/
	u8 ext[3];					    /* Extension*/
	u8 attr;						/* attribute bits */
	u8 lcase;						/* Case for base and extension */
	u8 ctime_cs;					/* creation time, centiseconds (0~199) */
	u16 ctime;						/* Creation time */
	u16 cdate;						/* Creation data */
	u16 adate;						/* Last access date */
	u16 starthi;					/* start cluster (Hight 16 bits) */
	u16 time; 						/* last modify time */
	u16 date;						/* last modify date */
	u16 startlow;					/* start cluster (Low 16 bits) */
	u32 size;						/* file size (in bytes) */
};

/* the directory of FAT32 */
union dir_entry {
	u8 data[32];
	struct dir_entry_attr attr;
};

/* file struct */
typedef struct fat_file {
	u8 path[256];//绝对路径
	/* Current file pointer */
	u32 loc;
	/* Current directory entry position */
	u32 dir_entry_pos;
	u32 dir_entry_sector;
	/* current directory entry */
	union dir_entry entry;
	/* Buffer clock head */
	u32 clock_head;
	/* For normal FAT32, cluster size is 4K */
	BUF_4K data_buf[LOCAL_DATA_BUF_NUM];
}FILE;

/* file system directory attribute */
typedef struct fs_fat_dir {
	u32 cur_sector;
	u32 loc;
	u32 sec;
}FS_FAT_DIR;

struct __attribute__((__packed__)) BPB_attr {
	// 0x00 ~ 0x0f
	u8 jump_code[3];			//3字节，跳转指令
	u8 oem_name[8];				//8字节，文件系统标志和版本号
	u16 sector_size;			//2字节 每扇区字节数
	u8 sectors_per_cluster;		//1字节 每簇扇区数
	u16 reserved_sectors;		//2字节 保留扇区数
	// 0x10 ~ 0x1f
	u8 number_of_copies_of_fat;	//1字节，FAT表个数
	u16 max_root_dir_entries;	//2字节，FAT32必须等于0
	u16 num_of_small_sectors;	//2字节，FAT32必须等于0
	u8 media_descriptor;		//1字节，哪种存储介质
	u16 sectors_per_fat;		//2字节，FAT32必须等于0
	u16 sectors_per_track;		//2字节，每磁道扇区数
	u16 num_of_heads;			//2字节，磁头数
	u32 num_of_hidden_sectors;
	//0x20 ~ 0x2f
	u32 num_of_sectors;			//4字节，文件总扇区数
	u32 num_of_sectors_per_fat; //4字节，每个FAT表占用扇区数
	u16 flags;					//标记，此域FAT32特有
	u16 version;				//2字节，FAT特有
	u32 cluster_number_of_root_dir;//4字节，根目录所在第一个簇的簇号
	//0x30 ~ 0x3f
	u16 sector_number_of_fs_info; //2字节，fsinfo扇区号0x01,该扇区为操作系统提供空簇总数，以及下一可用簇的信息
	u16 sector_number_of_backup_boot; //2字节，备份引导扇区的位置
	u8 reserved_data[12];		//12字节，用于以后FAT扩展使用
	//0x40 ~ 0x51
	u8 logical_drive_number;	//1字节
	u8 unused;					//1字节
	u8 extened_signature;		//1字节，扩展引导标志
	u32 serial_number;			//卷列序号
	u8 volume_name[11];			//11字节，卷标
	//0x52 ~ 0x1fe
	u8 fat_name[8];				//8字节，文件系统格式的ASCII码
	u8 exec_code[420];
	u8 boot_record_signature[2]; //签名标志"55AA"
};

union BPB_info {
	u8 data[512];
	struct BPB_attr attr;
};

/* The information of file system */
struct fs_info {
	u32 base_addr;
	u32 sectors_per_fat;
	u32 total_sectors;
	u32 total_data_clusters;
	u32 total_data_sectors;
	u32 first_data_sector;
	union BPB_info BPB;
	u8 fat_fs_info[SECTOR_SIZE];
};

/* find file */
unsigned long fs_find(FILE *file);

/* initial file system */
unsigned long init_fs();

/* open the file */
unsigned long fs_open(FILE *file, unsigned char *filename);

/* close the file */
unsigned long fs_close(FILE *file);

/* read the file */
unsigned long fs_read(FILE *file, unsigned char *buf, unsigned long count);

/* write the file */
unsigned long fs_write(FILE *file, const unsigned char *buf, unsigned long count);

unsigned long fs_fflush();

/* lseek in the file */
void fs_lseek(FILE *file, unsigned long new_loc);

/* create a new file */
unsigned long fs_create(unsigned char *filename);

/* make a new dir */
unsigned long fs_mkdir(unsigned char *filename);

/* remove the dir */
unsigned long fs_rmdir(unsigned char *filename);

/* remove the file */
unsigned long fs_rm(unsigned char *filename);

/* create a new file */
unsigned long fs_touch(unsigned char *path);

/* move file from one position to another position */
unsigned long fs_mv(unsigned char *src, unsigned char *dest);

/* copy a file */
unsigned long fs_cp(unsigned char *src, unsigned char *dest);

/* create a hard link */
unsigned long fs_ln(unsigned char *src, unsigned char *filename);

/* open the fsdir */
unsigned long fs_open_dir(FS_FAT_DIR *dir, unsigned char *filename);

/* read the fsdir */
unsigned long fs_read_dir(FS_FAT_DIR *dir, unsigned char *buf);

/* cat the file */
unsigned long fs_cat(unsigned char *path);

/* get the file name */
void get_filename(unsigned char *entry, unsigned char *buf);

/* get the file date */
void get_filedate(unsigned char *entry, unsigned char *buf);

/* get the file size */
void get_filesize(unsigned char *entry, unsigned char *buf);

/* get the file time */
void get_filetime(unsigned char *entry, unsigned char *buf);

/* get the file attr */
void get_fileattr(unsigned char *entry, unsigned char *buf);

u32 read_block(u8 *buf, u32 addr, u32 count);

u32 write_block(u8 *buf, u32 addr, u32 count);

u32 fs_alloc(u32 *new_alloc);

u32 get_entry_filesize(u8 *entry);

u32 get_entry_attr(u8 *entry);

#endif