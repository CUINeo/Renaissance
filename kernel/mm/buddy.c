#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/list.h>
#include <zjunix/lock.h>
#include <zjunix/utils.h>

unsigned int kernel_start_pfn, kernel_end_pfn;

struct page *pages;
struct buddy_sys buddy;

// print out information of buddy system
void buddy_info() {
    unsigned int index;
    kernel_printf("buddy-system:\n");
    kernel_printf("\tstart frame number: %x\n", buddy.buddy_start_pfn);
    kernel_printf("\tend frame number: %x\n", buddy.buddy_end_pfn);
    for (index = 0; index <= MAX_BUDDY_ORDER; ++index) {
        kernel_printf("\tlevel %x: %x frees\n", index, buddy.freelist[index].nr_free);
    }
}

// init all memory with page struct
void init_pages(unsigned int start_pfn, unsigned int end_pfn) {
    unsigned int index;
    for (index = start_pfn; index < end_pfn; index++) {
        clean_flag(pages + index, -1);      // all 0
        set_flag(pages + index, BUDDY_RESERVED);
        (pages + index)->reference = 1;
        (pages + index)->virtual = (void *)(-1);
        (pages + index)->bplevel = (-1);
        (pages + index)->slabp = 0;         // initially, the free space is the while page
        INIT_LIST_HEAD(&(pages[index].list));
    }
}

void init_buddy() {
    unsigned int bpsize = sizeof(struct page);
    unsigned char *bp_base;
    unsigned int i;

    bp_base = bootmm_alloc_pages(bpsize * bmm.max_pfn, _MM_KERNEL, 1 << PAGE_SHIFT);
    if (!bp_base) {
        // the remaining memory must be large enough to allocate the whole group
        // of buddy system
        kernel_printf("\nERROR : bootmm_alloc_pages failed!\nInit buddy system failed!\n");
        while (1)
            ;
    }

    // get virtual address for pages array
    pages = (struct page *)((unsigned int)bp_base | 0x80000000);

    init_pages(0, bmm.max_pfn);

    kernel_start_pfn = 0;
    kernel_end_pfn = 0;
    for (i = 0; i < bmm.cnt_infos; i++) {
        if (bmm.info[i].end > kernel_end_pfn) {
            kernel_end_pfn = bmm.info[i].end;
        }
    }

    // get physical end page frame number of kernel part
    kernel_end_pfn >>= PAGE_SHIFT;

    // remove the memory for the kernel(kernel_end_pfn + 1111 and clear last 4 digits)
    buddy.buddy_start_pfn = (kernel_end_pfn + (1 << MAX_BUDDY_ORDER) - 1) &
                            ~((1 << MAX_BUDDY_ORDER) - 1);
    // remain 2 pages for I/O(clear last 4 digits)
    buddy.buddy_end_pfn = bmm.max_pfn & ~((1 << MAX_BUDDY_ORDER) - 1);

    // init freelists of all bplevels
    for (i = 0; i < MAX_BUDDY_ORDER + 1; i++) {
        buddy.freelist[i].nr_free = 0;
        INIT_LIST_HEAD(&(buddy.freelist[i].free_head));
    }
    buddy.start_page = pages + buddy.buddy_start_pfn;
    init_lock(&(buddy.lock));

    // free all pages of buddy system
    for (i = buddy.buddy_start_pfn; i < buddy.buddy_end_pfn; i++) {
        __free_pages(pages + i, 0);
    }
}

void __free_pages(struct page *pbpage, unsigned int bplevel) {
    /* page_idx -> the current page
     * group_idx -> the buddy group that current page is in
     */
    unsigned int page_idx, group_idx;
    unsigned int combined_idx, temp;
    struct page *group_page;

    lockup(&buddy.lock);

    page_idx = pbpage - buddy.start_page;

    while (bplevel < MAX_BUDDY_ORDER) {
        // find the group page
        group_idx = page_idx ^ (1 << bplevel);
        group_page = pbpage + (group_idx - page_idx);

        if (!_is_same_bplevel(group_page, bplevel)) {
            // found group page is not of the same level
            break;
        }
        list_del_init(&group_page->list);
        if (buddy.freelist[bplevel].nr_free > 0)
            --buddy.freelist[bplevel].nr_free;
        set_bplevel(group_page, -1);
        combined_idx = group_idx & page_idx;
        pbpage += (combined_idx - page_idx);
        page_idx = combined_idx;
        bplevel++;
    }

    set_bplevel(pbpage, bplevel);
    list_add(&(pbpage->list), &(buddy.freelist[bplevel].free_head));
    ++buddy.freelist[bplevel].nr_free;

    // debug information
#ifdef BUDDY_DEBUG
    kernel_printf("%x %x\n", pbpage->list.next, buddy.freelist[bplevel].free_head.next);
#endif

    unlock(&buddy.lock);
}

struct page *__alloc_pages(unsigned int bplevel) {
    unsigned int current_order, size;
    struct page *page, *buddy_page;
    struct freelist *fList;

    lockup(&buddy.lock);
    
    // critical section
    for (current_order = bplevel; current_order <= MAX_BUDDY_ORDER; current_order++) {
        fList = buddy.freelist + current_order;
        if (!list_empty(&(fList->free_head)))
            goto found;
    }

    // not found
    unlock(&buddy.lock);
    return 0;

found:
    page = container_of(fList->free_head.next, struct page, list);
    list_del_init(&(page->list));
    set_bplevel(page, bplevel);
    set_flag(page, BUDDY_ALLOCED);
    
    if (fList->nr_free > 0)
        --fList->nr_free;

    size = 1 << current_order;
    while (current_order > bplevel) {
        --fList;
        current_order--;
        size >>= 1;
        buddy_page = page + size;
        // add the remaining half into free list
        list_add(&(buddy_page->list), &(fList->free_head));
        ++fList->nr_free;
        set_bplevel(buddy_page, current_order);
    }

    unlock(&buddy.lock);
    return page;
}

void *alloc_pages(unsigned int freePages) {
    unsigned int bplevel = 0;
    if (!freePages)
        // empty alloc
        return 0;
    else {
        // get bplevel
        bplevel = 1;
        while (1 << bplevel < freePages)
            bplevel++;
    }

    // call internal function
    struct page *page = __alloc_pages(bplevel);

    if (!page)
        // alloc failed
        return 0;

    return (void *)((page - pages) << PAGE_SHIFT);
}

void free_pages(void *addr, unsigned int bplevel) {
    __free_pages(pages + ((unsigned int)addr >> PAGE_SHIFT), bplevel);
}

struct page *test_alloc_pages(unsigned int bplevel) {
    unsigned int current_order, size;
    struct page *page, *buddy_page;
    struct freelist *fList;

    lockup(&buddy.lock);
    
    // critical section
    for (current_order = bplevel; current_order <= MAX_BUDDY_ORDER; current_order++) {
        fList = buddy.freelist + current_order;
        if (!list_empty(&(fList->free_head)))
            goto found;
    }

    // not found
    unlock(&buddy.lock);
    kernel_printf("No available pages found!\n");
    return 0;

found:
    page = container_of(fList->free_head.next, struct page, list);
    list_del_init(&(page->list));
    set_bplevel(page, bplevel);
    set_flag(page, BUDDY_ALLOCED);
    
    if (fList->nr_free > 0)
        --fList->nr_free;

    kernel_printf("level of found block: %d\n", current_order);

    size = 1 << current_order;
    while (current_order > bplevel) {
        kernel_printf("split a level %d block into two level %d blocks\n", current_order, current_order-1);

        --fList;
        --current_order;
        size >>= 1;
        buddy_page = page + size;
        // add the remaining half into free list
        list_add(&(buddy_page->list), &(fList->free_head));
        ++fList->nr_free;
        set_bplevel(buddy_page, current_order);
    }

    kernel_printf("alloc success.\n");

    unlock(&buddy.lock);
    return page;
}

void test_free_pages(struct page *pbpage, unsigned int bplevel) {
    /* page_idx -> the current page
     * group_idx -> the buddy group that current page is in
     */
    unsigned int page_idx, group_idx;
    unsigned int combined_idx, temp;
    struct page *group_page;

    lockup(&buddy.lock);

    page_idx = pbpage - buddy.start_page;

    while (bplevel < MAX_BUDDY_ORDER) {
        // find the group page
        group_idx = page_idx ^ (1 << bplevel);
        group_page = pbpage + (group_idx - page_idx);

        if (!_is_same_bplevel(group_page, bplevel)) {
            // found group page is not of the same level
            break;
        }
        // kernel_printf("pbpage: %d\n", pbpage);
        // kernel_printf("groupidx: %d\n", group_idx);
        // kernel_printf("pageidx: %d\n", page_idx);

        kernel_printf("pair block index: %d\n", group_idx);
        // kernel_printf("pair block starting page: %d\n", group_page);
        kernel_printf("buddy level: %d\n", bplevel);

        list_del_init(&group_page->list);

        if (buddy.freelist[bplevel].nr_free > 0)
            --buddy.freelist[bplevel].nr_free;
        set_bplevel(group_page, -1);
        combined_idx = group_idx & page_idx;
        pbpage += (combined_idx - page_idx);
        page_idx = combined_idx;
        bplevel++;
    }

    set_bplevel(pbpage, bplevel);
    list_add(&(pbpage->list), &(buddy.freelist[bplevel].free_head));
    ++buddy.freelist[bplevel].nr_free;

// debug information
#ifdef BUDDY_DEBUG
    kernel_printf("%x %x\n", pbpage->list.next, buddy.freelist[bplevel].free_head.next);
#endif

    unlock(&buddy.lock);
    kernel_printf("free success.\n");
}