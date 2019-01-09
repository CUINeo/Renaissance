#include "fat.h"
#include <driver/vga.h>
#include <zjunix/log.h>
#include "utils.h"
#include "driver/sd.h"

#ifdef FS_DEBUG
#include <intr.h>
#include <zjunix/log.h>
#include "debug.h"
#include "fscache.h"
#endif  // ! FS_DEBUG

#define DIR_DATA_BUF_NUM 4
/* fat buffer clock head */
u32 fat_clock_head = 0;
BUF_512 fat_buf[FAT_BUF_NUM];

u8 filename11[13];
u8 new_alloc_empty[PAGE_SIZE];

BUF_512 dir_data_buf[DIR_DATA_BUF_NUM];
u32 dir_data_clock_head = 0;

struct fs_info fat_info;

u32 init_fat_info() {
	u8 meta_buf[512]; //512个字节

	/* Init bufs */
	kernel_memset(meta_buf, 0, sizeof(meta_buf));
	kernel_memset(&fat_info, 0, sizeof(struct fs_info));

	/* Get MBR sector */
	if(sd_read_block(meta_buf, 0, 1) == 1) {
		goto init_fat_info_err;
	}
	log(LOG_OK, "Get MBR sector");
	/* MBR前446个字节为启动方面的代码， 接下来是第一个分区FAT32，1个字节引导指示符， 接下来一个字节开始磁头，6个bit开始扇区，10bit开始柱面， 1个字节分区类型*/
	fat_info.base_addr = get_u32(meta_buf + 446 + 8);
	//逻辑扇区首地址0
	/* Get FAT BPB */
	if(read_block(fat_info.BPB.data, 0, 1) == 1) {
		goto init_fat_info_err;
	}
	log(LOG_OK, "Get FAT BPB");
#ifdef FS_DEBUG
	dump_bpb_info(&(fat_info.BPB.attr));
#endif

	/* Sector size(MBR[11]) must be SECTOR_SIZE bytes */
	if(fat_info.BPB.attr.sector_size != SECTOR_SIZE) {
		log(LOG_FAIL, "FAT32 Sector size must be %d bytes, but get %d bytes.", SECTOR_SIZE, fat_info.BPB.attr.sector_size);
		goto init_fat_info_err;
	}

	/* Determine FAT type */
	/* For FAT32, max root dir entries must be 0 */
	if(fat_info.BPB.attr.max_root_dir_entries != 0) {
		goto init_fat_info_err;
	}
	/* For FAT32, total sectors at BPB[0x16] is 0 */
	if(fat_info.BPB.attr.num_of_small_sectors != 0) {
		goto init_fat_info_err;
	}
	/* For FAT32, sectors per fat at BPB[0x16] is 0 */
	if(fat_info.BPB.attr.sectors_per_fat != 0) {
		goto init_fat_info_err;
	}
	/* If not FAT32, goto error state */
	u32 total_sectors = fat_info.BPB.attr.num_of_sectors;
	u32 reserved_sectors = fat_info.BPB.attr.reserved_sectors;
	u32 sectors_per_fat = fat_info.BPB.attr.num_of_sectors_per_fat;
	u32 total_data_sectors = total_sectors - reserved_sectors - sectors_per_fat * 2;
	u8 sectors_per_cluster = fat_info.BPB.attr.sectors_per_cluster;
	fat_info.total_data_sectors = fat_info.total_sectors - fat_info.first_data_sector;
	fat_info.total_data_clusters = total_data_sectors / sectors_per_cluster;
	if(fat_info.total_data_clusters < 65525) {
		goto init_fat_info_err;
	}
	/* Get root dir sector */
	fat_info.first_data_sector = reserved_sectors + sectors_per_fat * 2;
	log(LOG_OK, "Partition type determined: FAT32");

	/* Keep FSInfo in buf */
	read_block(fat_info.fat_fs_info, 1, 1);
	log(LOG_OK, "Get FSInfo sector");

#ifdef FS_DEBUG
	dump_fat_info(&(fat_info));
#endif

	/* Init success */
	return 0;
init_fat_info_err:
	return 1;
}

/* Init fat buffer */
void init_fat_buf() {
	int i = 0;
	for (i = 0; i < FAT_BUF_NUM; i++) {
		fat_buf[i].cur = 0xffffffff;
		fat_buf[i].state = 0;
	}
}

/* Init directory buffer */
void init_dir_buf() {
	int i = 0;
	for (i = 0; i < DIR_DATA_BUF_NUM; i++) {
		dir_data_buf[i].cur = 0xffffffff;
		dir_data_buf[i].state = 0;
	}
}

/* FAT Initialize */
u32 init_fs() {
	/* if success then return 1 or return 0 */
	u32 succ = init_fat_info();
	if (0 != succ)
		goto fs_init_err;
	init_fat_buf();
	init_dir_buf();
	return 0;

fs_init_err:
	log(LOG_FAIL, "File system init fail.");
	return 1;
}

/* Write current fat sector */ //两个FAT表, 写入文件分区表
u32 write_fat_sector(u32 index) {
	if((fat_buf[index].cur != 0xffffffff && ((fat_buf[index].state) & 0x02) != 0)) {
		/* Write FAT and FAT copy */ //加上baseaddr
		if(write_block(fat_buf[index].buf, fat_buf[index].cur, 1) == 1) {
			goto write_fat_sector_err;
		}
		//加上base addr
		if(write_block(fat_buf[index].buf, fat_info.BPB.attr.num_of_sectors_per_fat + fat_buf[index].cur, 1))
			goto write_fat_sector_err;
		fat_buf[index].state &= 0x01;
	}
	return 0;
write_fat_sector_err:
	return 1;
}

/* Read fat sector */
u32 read_fat_sector(u32 ThisFATSecNum) {
	u32 index;
	/* try to find in buffer */
	for (index = 0; (index < FAT_BUF_NUM) && (fat_buf[index].cur != ThisFATSecNum); index++)
		;
	/* if not in buffer, find victim & replace, otherwise set reference bit */
	if(index == FAT_BUF_NUM) {
		index = fs_victim_512(fat_buf, &fat_clock_head, FAT_BUF_NUM);

		if(write_fat_sector(index) == 1)
			goto read_fat_sector_err;

		if(read_block(fat_buf[index].buf, ThisFATSecNum, 1) == 1)
			goto read_fat_sector_err;

		fat_buf[index].cur = ThisFATSecNum;
		fat_buf[index].state = 1;
	}else
		fat_buf[index].state |= 0x01;

	return index;
read_fat_sector_err:
	return 0xffffffff;
}

/* path convertion */
u32 fs_next_slash(u8 *f) {
	u32 i, j, k;
	u8 chr11[13];
	//i slash位置
	for(i = 0; (*(f + i) != 0) && (*(f + i) != '/'); i++)
		;
	for (j = 0; j < 12; j++) {
		chr11[j] = 0;
		filename11[j] = 0x20;
	}
	for (j = 0; j < 12 && j < i; j++) {
		chr11[j] = *(f + j);
		if(chr11[j] >= 'a' && chr11[j] <= 'z')
			chr11[j] = (u8)(chr11[j] - 'a' + 'A');
	}
	chr11[12] = 0;

	for (j = 0; (chr11[j] != 0) && (j < 12); j++) {
		if(chr11[j] == '.')
			break;

		filename11[j] = chr11[j];
	}

	if(chr11[j] == '.') {
		j++;
		for (k = 8; (chr11[j] != 0) && (j < 12) && (k < 11); j++, k++) {
			filename11[k] = chr11[j];
		}
	}

	filename11[11] = 0;

	return i;
}

/* strcmp */
u32 fs_cmp_filename(const u8 *f1, const u8 *f2) {
	u32 i;
	for (i = 0; i < 11; i++) {
		if(f1[i] != f2[i])
			return 1;
	}

	return 0;
}

/* Find a file, only absolute path with starting '/' accepted */
u32 fs_find(FILE *file) {
	/* get the filepath of file */
	u8 *f = file->path;
	u32 next_slash;
	u32 i, k;
	u32 next_clus;
	u32 index;
	u32 sec;

	/* filepath does not start with '/' */
	if(*(f++) != '/')
		goto fs_find_err;

	index = fs_read_512(dir_data_buf, fs_dataclus2sec(2), &dir_data_clock_head, DIR_DATA_BUF_NUM);
	//kernel_printf("Dir_data_buf: %s", dir_data_buf);
	/* Open root directory */
	if (index == 0xffffffff)
		goto fs_find_err;

	/* Find directory entry */
	while (1) {
		file->dir_entry_pos = 0xFFFFFFFF;

		next_slash = fs_next_slash(f);

		while (1) {
			for (sec = 1; sec <= fat_info.BPB.attr.sectors_per_cluster; sec++) {
				/* Find directory entry in current cluster */
				for(i = 0; i < 512; i += 32) {
					//如果
					if (*(dir_data_buf[index].buf + i) == 0)
						{
							kernel_printf("empety dir\n");
							goto after_fs_find;
						}
					//kernel_printf("fuck you\n");
					/* Ignore long path */
					if (fs_cmp_filename(dir_data_buf[index].buf + i, filename11) == 0 && 
						((*(dir_data_buf[index].buf + i + 11) & 0x08) == 0)) {
						//kernel_printf("fuck cui fan\n");
						file->dir_entry_pos = i;
						// refer to the issue in fs_close()
						file->dir_entry_sector = dir_data_buf[index].cur;

						for (k = 0; k < 32; k++) 
							file->entry.data[k] = *(dir_data_buf[index].buf + i + k);

						goto after_fs_find;
					}
				}
				/* next sector in current cluster */
				if (sec < fat_info.BPB.attr.sectors_per_cluster) {
					index = fs_read_512(dir_data_buf, dir_data_buf[index].cur + 1, &dir_data_clock_head, DIR_DATA_BUF_NUM);
					if (index == 0xFFFFFFFF) 
						goto fs_find_err;
				} 
				else {
					/* Read next cluster of current directory */
					u32 tmp = fs_sec2dataclus(dir_data_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1);
					if (get_fat_entry_value(tmp, &next_clus) == 1) 
						goto fs_find_err;

					if (next_clus <= fat_info.total_data_clusters + 1) {
						index = fs_read_512(dir_data_buf, fs_dataclus2sec(next_clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
						if (index == 0xffffffff)
							goto fs_find_err;
					} else
						goto after_fs_find;
				}
			}
		}
		after_fs_find:
			/* If not found */
			if (file->dir_entry_pos == 0xFFFFFFFF)
				goto fs_find_ok;

			/* If path parsing completes */
			if (f[next_slash] == 0)
				goto fs_find_ok;

			/* If not a sub directory */
			if ((file->entry.data[11] & 0x10) == 0)
				goto fs_find_err;

			f += next_slash + 1;

			/* Open sub directory, high word(+20), low word(+26) */
			next_clus = get_start_cluster(file);

			if (next_clus <= fat_info.total_data_clusters + 1) {
				index = fs_read_512(dir_data_buf, fs_dataclus2sec(next_clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
				if (index == 0xffffffff)
					goto fs_find_err;
			} else 
				goto fs_find_err;
	}
fs_find_ok:
	return 0;
fs_find_err:
	return 1;
}

/* Open: just do initializing & fs_find */
u32 fs_open(FILE *file, u8 *filename) {
	u32 k;

	/* Local buffer initialize */
	for (k = 0; k < LOCAL_DATA_BUF_NUM; k++) {
		file->data_buf[k].cur = 0xffffffff;
		file->data_buf[k].state = 0;
	}

	file->clock_head = 0;

	for (k = 0; k < 256; k++)
		file->path[k] = 0;
	for (k = 0; k < 256 && filename[k] != 0; k++)
		file->path[k] = filename[k];

	file->loc = 0;

	if(fs_find(file) == 1)
	{
		kernel_printf("fs_find failed\n");
		goto fs_open_err;
	}

	/* If file not exists */
	if (file->dir_entry_pos == 0xFFFFFFFF)
		goto fs_open_err;

	return 0;
fs_open_err:
	return 1;
}

/* fflush, write global buffers to sd */
u32 fs_fflush() {
	u32 k;

	//FSInfo should add base_addr
	if (write_block(fat_info.fat_fs_info, 1, 1) == 1)
		goto fs_fflush_err;

	if (write_block(fat_info.fat_fs_info, 7, 1) == 1)
		goto fs_fflush_err;

	for (k = 0; k < FAT_BUF_NUM; k++)
		if (write_fat_sector(k) == 1)
			goto fs_fflush_err;

	for (k = 0; k < DIR_DATA_BUF_NUM; k++)
		if (fs_write_512(dir_data_buf + k) == 1)
			goto fs_fflush_err;

	return 0;
fs_fflush_err:
	return 1;
}

/* Close: write all buf in memory to SD */
u32 fs_close(FILE *file) {
	u32 i;
	u32 index;

	/* Write directory entry */
	index = fs_read_512(dir_data_buf, file->dir_entry_sector, &dir_data_clock_head, DIR_DATA_BUF_NUM);
	if (index == 0xffffffff)
		goto fs_close_err;
	dir_data_buf[index].state = 3;

	// Issue: need file->dir_entry to be local partition offset
	for (i = 0; i < 32; i++)
		*(dir_data_buf[index].buf + file->dir_entry_pos + i) = file->entry.data[i];
	/* do fflush to write global buffers */
	if (fs_fflush() == 1)
		goto fs_close_err;
	/* write local data buffer */
	for (i = 0; i < LOCAL_DATA_BUF_NUM; i++)
		if (fs_write_4k(file->data_buf + i) == 1)
			goto fs_close_err;

	return 0;
fs_close_err:
	return 1;
}

/* Read from file */
u32 fs_read(FILE *file, u8 *buf, u32 count) {
	u32 start_clus, start_byte;
	u32 end_clus, end_byte;
	u32 filesize = file->entry.attr.size;
	u32 clus = get_start_cluster(file);
	u32 next_clus;
	u32 i;
	u32 cc;
	u32 index;

#ifdef FS_DEBUG
	kernel_printf(" fs_read: count %d\n", count);
	disable_interrupts();
#endif	//! FS_DEBUG
	/* If file is empty */
	if (clus == 0)
		return 0;

	/* If loc + count > filesize, only up to EOF will be read */
	if (file->loc + count > filesize)
		count = filesize - file->loc;

	/* If read 0 byte */
	if (count == 0)
		return 0;

	start_clus = file->loc >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
	start_byte = file->loc & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);
	end_clus = (file->loc + count - 1) >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
	end_byte = (file->loc + count - 1) & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);

#ifdef FS_DEBUG
	kernel_printf("  start cluster: %d\n", start_clus);
	kernel_printf("  start byte: %d\n", start_byte);
	kernel_printf("  end cluster: %d\n", end_clus);
	kernel_printf("  end byte: %d\n", end_byte);
#endif // ! FS_DEBUG
	/* Open first cluster to read */
	for (i = 0; i < start_clus; i++) {
		if (get_fat_entry_value(clus, &next_clus) == 1)
			goto fs_read_err;

		clus = next_clus;
	}

	cc = 0;
	while (start_clus <= end_clus) {
		index = fs_read_4k(file->data_buf, fs_dataclus2sec(clus), &(file->clock_head), LOCAL_DATA_BUF_NUM);
		if (index == 0xffffffff) 
			goto fs_read_err;

		/* If in the same cluster, just read */
		if (start_clus == end_clus) {
			for (i = start_byte; i <= end_byte; i++)
				buf[cc++] = file->data_buf[index].buf[i];
			goto fs_read_end;
		}
		/* otherwise, read clusters one by one */
		else {
			for (i = start_byte; i < (fat_info.BPB.attr.sectors_per_cluster << 9); i++)
				buf[cc++] = file->data_buf[index].buf[i];

			start_clus++;
			start_byte = 0;

			if (get_fat_entry_value(clus, &next_clus) == 1)
				goto fs_read_err;

			clus = next_clus;
		}
	}
fs_read_end:

#ifdef FS_DEBUG
	kernel_printf("  fs_read: count %d\n", count);
	enable_interrupts();
#endif // ! FS_DEBUG
	/* modify file pointer */
	file->loc += count;
	return cc;
fs_read_err:
	return 0xFFFFFFFF;
}

/* Find a free data cluster */
u32 fs_next_free(u32 start, u32 *next_free) {
	u32 clus;
	u32 ClusEntryVal;

	*next_free = 0xFFFFFFFF;

	for (clus = start; clus <= fat_info.total_data_clusters + 1; clus++) {
		if (get_fat_entry_value(clus, &ClusEntryVal) == 1)
			goto fs_next_free_err;

		if (ClusEntryVal == 0) {
			*next_free = clus;
			break;
		}
	}

	return 0;
fs_next_free_err:
	return 1;
}

/* Alloc a new free data cluster */
u32 fs_alloc(u32 *new_alloc) {
	u32 clus;
	u32 next_free;

	clus = get_u32(fat_info.fat_fs_info + 492) + 1;

	/* If FSI_Nxt_Free is illegal (> FSI_Free_Count), find a free data cluster
	 * from beginning */
	if (clus > get_u32(fat_info.fat_fs_info + 488) + 1) {
		if (fs_next_free(2, &clus) == 1)
			goto fs_alloc_err;
		//alloc之后修改位0FFFFFFF表示结束
		if (fs_modify_fat(clus, 0xFFFFFFFF) == 1)
			goto fs_alloc_err;
	}

	/* FAT allocated and update FSI_Nxt_Free */
	if (fs_modify_fat(clus, 0xFFFFFFFF) == 1)
		goto fs_alloc_err;

	if (fs_next_free(clus, &next_free) == 1)
		goto fs_alloc_err;

	/* no available free cluster */
	if (next_free > fat_info.total_data_clusters + 1)
		goto fs_alloc_err;

	set_u32(fat_info.fat_fs_info + 492, next_free - 1);

	*new_alloc = clus;

	/* Erase new allocated cluster */
	if (write_block(new_alloc_empty, fs_dataclus2sec(clus), fat_info.BPB.attr.sectors_per_cluster) == 1)
		goto fs_alloc_err;

	return 0;
fs_alloc_err:
	return 1;
}

/* Write to file */
u32 fs_write(FILE *file, const u8 *buf, u32 count) {
	/* If write 0 byte */
	if (count == 0) 
		return 0;

	u32 start_clus = file->loc >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
	u32 start_byte = file->loc & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);
	u32 end_clus = (file->loc + count - 1) >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
	u32 end_byte = (file->loc + count - 1) & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);

	/* If file is empty, alloc a new data cluster */
	u32 curr_cluster = get_start_cluster(file);
	if (curr_cluster == 0) {
		if (fs_alloc(&curr_cluster) == 1) {
			goto fs_write_err;
		}
		file->entry.attr.starthi = (u16)(((curr_cluster >> 16) & 0xFFFF));
		file->entry.attr.startlow = (u16)((curr_cluster & 0xFFFF));
		if(fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(curr_cluster)) == 1)
			goto fs_write_err;
	}

	/* Open first cluster t read */
	u32 next_cluster;
	for (u32 i = 0; i < start_clus; i++) {
		if (get_fat_entry_value(curr_cluster, &next_cluster) == 1)
			goto fs_write_err;

		/* If this is the last cluster in file, and still need to open next
         * cluster, just alloc a new data cluster */
		if (next_cluster > fat_info.total_data_clusters + 1) {
			if (fs_alloc(&next_cluster) == 1)
				goto fs_write_err;

			if (fs_modify_fat(curr_cluster, next_cluster) == 1)
				goto fs_write_err;

			if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(next_cluster)) == 1)
				goto fs_write_err;
		}

		curr_cluster = next_cluster;
	}
	u32 cc = 0;
	u32 index = 0;
	while (start_clus <= end_clus) {
		index = fs_read_4k(file->data_buf, fs_dataclus2sec(curr_cluster), &(file->clock_head), LOCAL_DATA_BUF_NUM);
		if (index == 0xffffffff)
			goto fs_write_err;

		file->data_buf[index].state = 3;

		/* If in the same cluster, just write */
		if (start_clus == end_clus) {
			for (u32 i = start_byte; i <= end_byte; i++)
				file->data_buf[index].buf[i] = buf[cc++];
			goto fs_write_end;
		}
		/* otherwise, write clusters one by one */
		else {
			for (u32 i = start_byte; i < (fat_info.BPB.attr.sectors_per_cluster << 9); i++)
				file->data_buf[index].buf[i] = buf[cc++];
			start_clus++;
			start_byte = 0;

			if (get_fat_entry_value(curr_cluster, &next_cluster) == 1)
				goto fs_write_err;

			/* If this is the last cluster in file, and still need to open next
             * cluster, just alloc a new data cluster */
			if (next_cluster > fat_info.total_data_clusters + 1) {
                if (fs_alloc(&next_cluster) == 1)
                    goto fs_write_err;

                if (fs_modify_fat(curr_cluster, next_cluster) == 1)
                    goto fs_write_err;

                if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(next_cluster)) == 1)
                    goto fs_write_err;
            }

            curr_cluster = next_cluster;
		}
	}
fs_write_end:

    /* update file size */
    if (file->loc + count > file->entry.attr.size)
        file->entry.attr.size = file->loc + count;

    /* update location */
    file->loc += count;

    return cc;
fs_write_err:
    return 0xFFFFFFFF;
}

// lseek
void fs_lseek (FILE *file, u32 next_loc) {
	u32 size = file->entry.attr.size;

	//if next position is less than the file size, then replace the loc
	if (next_loc < size) {
		file->loc = next_loc;
	} 
	//the next postion is larger than than file size, go to the last
	else {
		file->loc = size;
	}
}

// find an empty directory entry
u32 fs_find_empty_entry(u32 *empty_entry, u32 index) {
	u32 nextclus; //the next clus
	u32 sector;	  //a sector
	u32 k;

	while(1) {
		for (sector = 1; sector <= fat_info.BPB.attr.sectors_per_cluster; sector++) //遍历簇中所有扇区
		{
			//在当前簇中搜索目录项
			for (k = 0; k < 512; k+=32)
			{
				if((*(dir_data_buf[index].buf + k) == 0) || (*(dir_data_buf[index].buf + k) == 0xE5)) 
				{
					*empty_entry = k;
					goto after_fs_find_empty_entry;
				}
			}
			//在上一个扇区没有找到, 且当前簇没有遍历结束
			if (sector < fat_info.BPB.attr.sectors_per_cluster)
			{
				//找一个新的扇区
				index = fs_read_512(dir_data_buf, dir_data_buf[index].cur + 1, &dir_data_clock_head, DIR_DATA_BUF_NUM);
				if (0xffffffff == index)
				{
					goto fs_find_empty_entry_err;
				}
			} 
			else //当前簇遍历结束，读取下一簇
			{
				//我们知道该目录项，存储着下一簇的地址，故需从当前目录项，读取下一簇的位置
				u32 tmp = fs_sec2dataclus(dir_data_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1);
				if (get_fat_entry_value(tmp, &nextclus) == 1) 
				{
					goto fs_find_empty_entry_err;
				}
				if (nextclus > fat_info.total_data_clusters + 1)
				{
					if (fs_alloc(&nextclus) == 1)
						goto fs_find_empty_entry_err;
					if (fs_modify_fat(fs_sec2dataclus(dir_data_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1), nextclus) == 1)
						goto fs_find_empty_entry_err;

					*empty_entry = 0;

					if (fs_clr_512(dir_data_buf, &dir_data_clock_head, DIR_DATA_BUF_NUM, fs_dataclus2sec(nextclus)) == 1)
						goto fs_find_empty_entry_err;
				}
				index = fs_read_512(dir_data_buf, fs_dataclus2sec(nextclus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
				if (index == 0xffffffff)
					goto fs_find_empty_entry_err;
			}
		}
	}
after_fs_find_empty_entry:
	return index;
fs_find_empty_entry_err:
	return 0xffffffff;
}

//创建新文件
u32 fs_create_with_attr (u8 *filename, u8 attr) {
	u32 i;
	u32 l1 = 0;
	u32 l2 = 0;
	u32 empty_entry;
	u32 clus;
	u32 index;
	FILE file_create;

	//首先判断文件是否存在, 如果能打开该文件证明文件存在
	if (fs_open(&file_create, filename) == 0)
	{
		kernel_printf("The file has exists\n");
		goto fs_create_err;
	}
	for (i = 255; i >= 0; i--)
	{
		if (file_create.path[i] != 0) {
			l2 = i;
			break;
		}
	}

	for (i = 255; i >= 0; i--)
	{
		if (file_create.path[i] == '/')
		{
			l1 = i;
			break;
		}
	}
	//不是根目录，找到对应目录
	if (l1 != 0)
	{
		for (i = l1; i <= 12; i++)
			file_create.path[i] = 0;
		if (fs_find(&file_create) == 1)
			goto fs_create_err;
		if (file_create.dir_entry_pos == 0xFFFFFFFF)
			goto fs_create_err;

		clus = get_start_cluster(&file_create);
		//打开文件夹
		index = fs_read_512(dir_data_buf, fs_dataclus2sec(clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
		if (index == 0xffffffff)
			goto fs_create_err;

		file_create.dir_entry_pos = clus;
	}
	//打开根目录
	else
	{
		index = fs_read_512(dir_data_buf, fs_dataclus2sec(2), &dir_data_clock_head, DIR_DATA_BUF_NUM);
        if (index == 0xffffffff)
        {
            goto fs_create_err;
        }

		file_create.dir_entry_pos = 2;
	}
	kernel_printf("Begin Find empty entry\n");
	//为新文件找一个空的目录项
	index = fs_find_empty_entry(&empty_entry, index);
	kernel_printf("Has Find empty entry\n");
	if (index == 0xffffffff)
		goto fs_create_err;

	for (i = l1 + 1; i <= l2; i++)
		file_create.path[i - l1 -1] = filename[i];
	file_create.path[l2-l1] = 0;
	fs_next_slash(file_create.path);
	dir_data_buf[index].state = 3;

	for (i = 0; i < 11; i++)
		*(dir_data_buf[index].buf + empty_entry + i) = filename11[i];
	*(dir_data_buf[index].buf + empty_entry + 11) = attr;
	//other should be zero
	for (i = 12; i < 32; i++)
		*(dir_data_buf[index].buf + empty_entry + i) = 0;

	if (fs_fflush() == 1)
		goto fs_create_err;

	return 0;

fs_create_err:
	return 1;
}

u32 fs_create(u8 *filename)
{
	return fs_create_with_attr(filename, 0x20);
}

void get_filetime(u8 *entry, u8 *buf) {
	u32 i;
	for (i = 22; i < 24; i++)
		buf[i-22] = entry[i];

	buf[i-22] = 0;
}

void get_filedate(u8 *entry, u8 *buf) {
	u32 i;
	for (i = 24; i < 26; i++)
		buf[i-28] = entry[i];

	buf[i-28] = 0;
}

void get_filename(u8 *entry, u8 *buf) {
    u32 i;
    u32 l1 = 0, l2 = 8;

    for (i = 0; i < 11; i++)
        buf[i] = entry[i];

    if (buf[0] == '.') {
        if (buf[1] == '.')
            buf[2] = 0;
        else
            buf[1] = 0;
    } else {
        for (i = 0; i < 8; i++)
            if (buf[i] == 0x20) {
                buf[i] = '.';
                l1 = i;
                break;
            }

        if (i == 8) {
            for (i = 11; i > 8; i--)
                buf[i] = buf[i - 1];

            buf[8] = '.';
            l1 = 8;
            l2 = 9;
        }

        for (i = l1 + 1; i < l1 + 4; i++) {
            if (buf[l2 + i - l1 - 1] != 0x20)
                buf[i] = buf[l2 + i - l1 - 1];
            else
                break;
        }

        buf[i] = 0;

        if (buf[i - 1] == '.')
            buf[i - 1] = 0;
    }
}

void get_fileattr(u8 *entry, u8 *buf)
{
	buf[0] = entry[11];
	buf[1] = 0;
}