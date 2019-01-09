#include "utils.h"
#include <driver/sd.h>
#include "fat.h"

//从一个分区第一个块开始，写块
u32 write_block(u8 *buf, u32 addr, u32 count) {
    addr += fat_info.base_addr;
    return sd_write_block(buf, addr, count);
}

//从第一个分区第一个块开始读块
u32 read_block(u8 *buf, u32 addr, u32 count) {
    addr += fat_info.base_addr;
    return sd_read_block(buf, addr, count);
}

//将u16转换成u8
void set_u16(u8 *ch, u16 num) {
    *ch = (u8)(num & 0xFF);
    *(ch + 1) = (u8)((num >> 8) & 0xFF);
}

//将u32转换成u8
void set_u32(u8 *ch, u32 num) {
    *ch = (u8)(num & 0xFF);
    *(ch + 1) = (u8)((num >> 8) & 0xFF);
    *(ch + 2) = (u8)((num >> 16) & 0xFF);
    *(ch + 3) = (u8)((num >> 24) & 0xFF);
}

//将u8转换成u32
u32 get_u32(u8 *ch) {
    return (*ch) + ((*(ch + 1)) << 8) + ((*(ch + 2)) << 16) + ((*(ch + 3)) << 24);
}

//将u8转换成u16
u16 get_u16(u8 *ch) {
    return (*ch) + ((*(ch + 1)) << 8);
}

//左移获取位数
u32 fs_wa(u32 num) {
    u32 bits;
    for (bits = 0; num > 1; num >>= 1, bits++)
        ;
    return bits;
}


//struct __attribute__((__packed__)) dir_entry_attr 
//{
	// u8 name[8];					    /* Name 8 char*/
	// u8 ext[3];					    /* Extension*/
	// u8 attr;						/* attribute bits */
	// u8 lcase;						/* Case for base and extension */
	// u8 ctime_cs;					/* creation time, centiseconds (0~199) */
	// u16 ctime;						 Creation time 
	// u16 cdate;						/* Creation data */
	// u16 starthi;					/* start cluster (Hight 16 bits) */
	// u16 time; 						/* last modify time */
	// u16 date;						/* last modify date */
	// u16 startlow;					/* start cluster (Low 16 bits) */
	//u32 size;						/* file size (in bytes) */
//}

//获取文件大小
u32 get_entry_filesize(u8 *entry) {
    return get_u32(entry + 28);
}

//获取文件属性位
u32 get_entry_attr(u8 *entry) {
	return entry[11];
}

//获取目录项值
u32 get_fat_entry_value(u32 clus, u32 *ClusEntryVal) {
    u32 ThisFATSecNum;
    u32 ThisFATEntOffset;
    u32 index;

    cluster_to_fat_entry(clus, &ThisFATSecNum, &ThisFATEntOffset);

    index = read_fat_sector(ThisFATSecNum);
    if (index == 0xffffffff)
        goto get_fat_entry_value_err;

    *ClusEntryVal = get_u32(fat_buf[index].buf + ThisFATEntOffset) & 0x0FFFFFFF;

    return 0;
get_fat_entry_value_err:
    return 1;
}

//找到开始簇
u32 get_start_cluster(const FILE *file) {
    return (file->entry.attr.starthi << 16) + (file->entry.attr.startlow);
}

//当簇发生改变时，修改fat表
u32 fs_modify_fat(u32 clus, u32 ClusEntryVal) {
    u32 ThisFATSecNum;
    u32 ThisFATEntOffset;
    u32 fat32_val;
    u32 index;

    cluster_to_fat_entry(clus, &ThisFATSecNum, &ThisFATEntOffset);

    index = read_fat_sector(ThisFATSecNum);
    if (index == 0xffffffff)
        goto fs_modify_fat_err;

    fat_buf[index].state = 3;

    ClusEntryVal &= 0x0FFFFFFF;
    fat32_val = (get_u32(fat_buf[index].buf + ThisFATEntOffset) & 0xF0000000) | ClusEntryVal;
    set_u32(fat_buf[index].buf + ThisFATEntOffset, fat32_val);

    return 0;
fs_modify_fat_err:
    return 1;
}

void cluster_to_fat_entry(u32 clus, u32 *ThisFATSecNum, u32 *ThisFATEntOffset) {
    u32 FATOffset = clus << 2;
    *ThisFATSecNum = fat_info.BPB.attr.reserved_sectors + (FATOffset >> 9);
    *ThisFATEntOffset = FATOffset & 511;
}

//扇区与簇之间的相互转换
u32 fs_sec2dataclus(u32 sec) {
    return ((sec - fat_info.first_data_sector) >> fs_wa(fat_info.BPB.attr.sectors_per_cluster)) + 2;
}

u32 fs_dataclus2sec(u32 clus) {
    return ((clus - 2) << fs_wa(fat_info.BPB.attr.sectors_per_cluster)) + fat_info.first_data_sector;
}