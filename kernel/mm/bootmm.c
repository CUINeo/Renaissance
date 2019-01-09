#include <arch.h>
#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/utils.h>

struct bootmm bmm;
unsigned int firstusercode_start;
unsigned int firstusercode_len;

// const value for ENUM of mem_type
char *mem_msg[] = {"Kernel code/data", "Mm Bitmap", "Vga Buffer", "Kernel page directory", "Kernel page table", "Dynamic", "Reserved"};

// set the content of struct bootmm_info
void set_mminfo(struct bootmm_info *info, unsigned int start, unsigned int end, unsigned int type) {
    info->start = start;
    info->end = end;
    info->type = type;
}

// MACHINE_MMSIZE >> PAGE_SHIFT -> machine page number
unsigned char bootmmmap[MACHINE_MMSIZE >> PAGE_SHIFT];

/*
* return value list:
*		0 -> insert_mminfo failed
*		1 -> insert non-related mm_segment
*		2 -> insert forward-connecting segment
*		4 -> insert following-connecting segment
*		7 -> insert bridge-connecting segment(remove_mminfo is called
for deleting
--
*/
unsigned int insert_mminfo(struct bootmm *mm, unsigned int start, unsigned int end, unsigned int type) {
    unsigned int i;
    struct bootmm_info *Infos = mm->info;

    for (i = 0; i < mm->cnt_infos; i++) {
        if (Infos[i].start > start)
            // inserting block is in front of Infos[i]
            break;
        if (Infos[i].type != type)
            // blocks with different types
            continue;
        if (Infos[i].end == start - 1) {
            // inserting block is the following one of Infos[i]
            if (i + 1 < mm->cnt_infos) {
                // i+1 not out of bound
                if (Infos[i+1].type == type && Infos[i+1].start == end + 1) {
                    // Infos[i+1] is the following one of inserting block
                    // two-way merge (Infos[i] + inserting block + Infos[i+1])
                    Infos[i].end = Infos[i+1].end;
                    remove_mminfo(mm, i+1);
                    return 7;
                }
            } 
            // forward merge
            Infos[i].end = end;
            return 2;
        }
    }
    if (i >= MAX_INFO)
        // full
        return 0;
    if (i < mm->cnt_infos) {
        if (Infos[i].type == type && Infos[i].start == end + 1) {
            // Infos[i] is the following one of inserting block
            // backward merge
            Infos[i].start = start;
            return 4;
        }
    }
    // new a block and add it to the tail of the list
    // non-related mm_segment
    set_mminfo(Infos + mm->cnt_infos, start, end, type);
    mm->cnt_infos++;
    return 1;
}

/* get one sequential memory area to be split into two parts
 * (set the former one.end = split_start-1)
 * (set the latter one.start = split_start)
 */
unsigned int split_mminfo(struct bootmm *mm, unsigned int index, unsigned int split_start) {
    unsigned int start, end, temp;

    if (index >= mm->cnt_infos)
        // invalid index
        return 0;

    start = mm->info[index].start;
    end = mm->info[index].end;

    // Cannot split blocks within page
    split_start &= PAGE_ALIGN;

    if (split_start <= start || split_start >= end)
        // split_start not in info[index]
        return 0;
    if (mm->cnt_infos == MAX_INFO)
        // info array is full
        return 0;

    for (temp = mm->cnt_infos-1; temp >= index; temp--)
        mm->info[temp + 1] = mm->info[temp];

    mm->info[index].end = split_start - 1;
    mm->info[index + 1].start = split_start;
    mm->cnt_infos++;
    return 1;
}

// remove mm->info[index]
void remove_mminfo(struct bootmm *mm, unsigned int index) {
    unsigned int i;
    if (index >= mm->cnt_infos)
        // invalid index
        return;

    if (index + 1 < mm->cnt_infos) {
        for (i = index + 1; i < mm->cnt_infos; i++) {
            mm->info[i - 1] = mm->info[i];
        }
    }
    mm->cnt_infos--;
}

void init_bootmm() {
    unsigned int index;
    unsigned char *t_map;
    unsigned int end;
    end = 16 * 1024 * 1024; // 16 MB for kernel
    kernel_memset(&bmm, 0, sizeof(bmm));
    bmm.phymm = get_phymm_size();
    bmm.max_pfn = bmm.phymm >> PAGE_SHIFT;
    bmm.s_map = bootmmmap;
    bmm.e_map = bootmmmap + sizeof(bootmmmap);
    bmm.cnt_infos = 0;
    kernel_memset(bmm.s_map, PAGE_FREE, sizeof(bootmmmap));
    insert_mminfo(&bmm, 0, (unsigned int)(end - 1), _MM_KERNEL);
    bmm.last_alloc_end = (((unsigned int)(end) >> PAGE_SHIFT) - 1);

    for (index = 0; index < (end >> PAGE_SHIFT); index++) {
        bmm.s_map[index] = PAGE_USED;
    }

// #ifdef BOOTMM_DEBUG
    // kernel_printf("bootmm init complete.\n");
// #endif
}

/*
 * set value of page-bitmap-indicator
 * @param s_pfn	: page frame start node
 * @param cnt	: the number of pages to be set
 * @param value	: the value to be set
 */
void set_maps(unsigned int s_pfn, unsigned int cnt, unsigned char value) {
    while (cnt) {
        bmm.s_map[s_pfn] = (unsigned char)value;
        cnt--;
        s_pfn++;
    }
}

/*
 * This function is to find sequential page_cnt number of pages to allocate
 * @param page_cnt : the number of pages requested
 * @param s_pfn    : the allocating begin page frame node
 * @param e_pfn	   : the allocating end page frame node
 * return value  = 0 :: allocate failed, else return index(page start)
 */
unsigned char *find_pages(unsigned int page_cnt, unsigned int s_pfn, unsigned int e_pfn, unsigned int align_pfn) {
    unsigned int index, temp, cnt;

    s_pfn += (align_pfn - 1);
    s_pfn &= ~(align_pfn - 1);

    for (index = s_pfn; index < e_pfn; ) {
        if (bmm.s_map[index] == PAGE_USED) {
            index++;
            continue;
        }

        cnt = page_cnt;
        temp = index;
        while (cnt) {
            if (temp >= e_pfn)
                // reaching end, but allocate request still cannot be satisfied
                return 0;
            if (bmm.s_map[temp] == PAGE_FREE) {
                // find next possible free page
                temp++;
                cnt--;
            }
            if (bmm.s_map[temp] == PAGE_USED)
                // not enough space
                break;
        }
        if (cnt == 0) {
            // cnt = 0 indicates that the specified page-sequence found
            bmm.last_alloc_end = temp - 1;
            set_maps(index, page_cnt, PAGE_USED);
            return (unsigned char *)(index << PAGE_SHIFT);
        }
        else {
            // no possible memory space to be allocated before temp
            index = temp + align_pfn;
        }
    }
    // find failed
    return 0;
}

void bootmap_info() {
    unsigned int index;
    kernel_printf("Bootmm system:\n");
    for (index = 0; index < bmm.cnt_infos; index++) {
        kernel_printf("\t%x-%x : %s\n", bmm.info[index].start, bmm.info[index].end, mem_msg[bmm.info[index].type]);
    }
}

unsigned char *bootmm_alloc_pages(unsigned int size, unsigned int type, unsigned int align) {
    unsigned int size_inpages;
    unsigned char *res;

    size += ((1 << PAGE_SHIFT) - 1);
    size &= PAGE_ALIGN;
    size_inpages = size >> PAGE_SHIFT;

    // in normal case, going forward is most likely to find suitable area
    res = find_pages(size_inpages, bmm.last_alloc_end + 1, bmm.max_pfn, align >> PAGE_SHIFT);
    if (res) {
        insert_mminfo(&bmm, (unsigned int)res, (unsigned int)res + size - 1, type);
        return res;
    }

    // when system requests a lot of operations in booting, then some free area
    // will appear in the front part
    res = find_pages(size_inpages, 0, bmm.last_alloc_end, align >> PAGE_SHIFT);
    if (res) {
        insert_mminfo(&bmm, (unsigned int)res, (unsigned int)res + size - 1, type);
        return res;
    }
    // not found, return NULL
    return 0;
}

// useless function
void bootmm_free_pages(unsigned int start, unsigned int size) {
    unsigned int index, size_inpages;
    struct bootmm_info rem;
    size &= PAGE_ALIGN;
    size_inpages = size >> PAGE_SHIFT;

    if (!size_inpages)
        // space less than one page, no need to free
        return;

    // starting from a page head
    start &= PAGE_ALIGN;
    for (index = 0; index < bmm.cnt_infos; index++) {
        if (bmm.info[index].start <= start && bmm.info[index].end >= start + size - 1)
            // find the block to be removed
            break;
    }
    if (index == bmm.cnt_infos) {
        kernel_printf("bootmm_free_pages: no allocated space %x-%x\n", start, start + size - 1);
        return;
    }

    rem = bmm.info[index];
    if (rem.start == start) {
        if (rem.end == start + size - 1)
            // exactly the same
            remove_mminfo(&bmm, index);
        else        
            // remove the front part
            set_mminfo(bmm.info + index, start + size, rem.end, rem.type);
    }
    else if (rem.end == start + size - 1)    
        // remove the rear part
        set_mminfo(bmm.info + index, rem.start, start - 1, rem.type);
    else {
        if (split_mminfo(&bmm, index, start) == 0) {
            kernel_printf("bootmm_free_pages: split fail\n");
            return;
        }
        set_mminfo(bmm.info + index + 1, start + size, rem.end, rem.type);
    }
    set_maps(start<<PAGE_SHIFT, size_inpages, PAGE_FREE);
}
