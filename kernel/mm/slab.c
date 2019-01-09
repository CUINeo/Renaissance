#include <arch.h>
#include <driver/vga.h>
#include <zjunix/slab.h>
#include <zjunix/utils.h>

#define KMEM_ADDR(PAGE, BASE) ((((PAGE) - (BASE)) << PAGE_SHIFT) | 0x80000000)

/*
 * one list of PAGE_SHIFT(now it's 12) possbile memory size
 * 96, 192, 8, 16, 32, 64, 128, 256, 512, 1024, (2 undefined)
 * in current stage, set (2 undefined) to be (4, 2048)
 */
struct kmem_cache kmalloc_caches[PAGE_SHIFT];

static unsigned int size_kmem_cache[PAGE_SHIFT] = {96, 192, 8, 16, 32, 64, 128, 256, 512, 1024, 1536, 2048};

// init the struct kmem_cache_cpu
void init_kmem_cpu(struct kmem_cache_cpu *kcpu) {
    kcpu->page = 0;
}

// init the struct kmem_cache_node
void init_kmem_node(struct kmem_cache_node *knode) {
    INIT_LIST_HEAD(&(knode->full));
    INIT_LIST_HEAD(&(knode->partial));
}

void init_each_slab(struct kmem_cache *cache, unsigned int size) {
    cache->objsize = size;
    cache->objsize += (SIZE_INT - 1);
    cache->objsize &= ~(SIZE_INT - 1);
    cache->size = cache->objsize + sizeof(void *);  // add one pointer to link free objects
    cache->offset = cache->size;
    init_kmem_cpu(&(cache->cpu));
    init_kmem_node(&(cache->node));
}

void init_slab() {
    unsigned int i;

    for (i = 0; i < PAGE_SHIFT; i++) {
        init_each_slab(&(kmalloc_caches[i]), size_kmem_cache[i]);
    }
#ifdef SLAB_DEBUG
    kernel_printf("Setup Slub ok :\n");
    kernel_printf("\tcurrent slab cache size list:\n\t");
    for (i = 0; i < PAGE_SHIFT; i++) {
        kernel_printf("%x %x ", kmalloc_caches[i].objsize, (unsigned int)(&(kmalloc_caches[i])));
    }
    kernel_printf("\n");
#endif  // ! SLAB_DEBUG
}

void format_slabpage(struct kmem_cache *cache, struct page *page) {
    unsigned char *moffset = (unsigned char *)KMEM_ADDR(page, pages);   //get physical addr

    set_flag(page, BUDDY_SLAB);

    struct slab_head *s_head = (struct slab_head *)moffset;
    void *ptr = moffset + sizeof(struct slab_head);

    s_head->end_ptr = ptr;
    s_head->nr_objs = 0;
    s_head->was_full = 0;

    cache->cpu.page = page;
    page->virtual = (void *)cache;  // used in k_free
    page->slabp = 0;                // points to the free object list in this page
}

void *slab_alloc(struct kmem_cache *cache) {
    void *object = 0;
    struct page *current_page;
    struct slab_head *s_head;

    if (!cache->cpu.page)
        // no available page
        goto PageFull;

    current_page = cache->cpu.page;
    s_head = (struct slab_head *)KMEM_ADDR(current_page, pages);

#ifdef SLAB_DEBUG
    if (s_head->was_full)
        kernel_printf("The page was full.\n");
    else
        kernel_printf("The page was never full.\n");
#endif

FromFreeList:
    // allocate from free pages
    if (current_page->slabp != 0) {
        object = (void *)current_page->slabp;
        current_page->slabp = *(unsigned int *)current_page->slabp;
        s_head->nr_objs++;
#ifdef SLAB_DEBUG
        kernel_printf("From Free list\nnr_objs:%d\tobject:%x\tnew slabp:%x\n",
                    s_head->nr_objs, object, current_page->slabp);
#endif  // ! SLAB_DEBUG
        return object;
    }

PageNotFull:
    if (!s_head->was_full) {
        // uninitialized space
        object = s_head->end_ptr;
        s_head->end_ptr = object + cache->size;
        s_head->nr_objs++;

        if (s_head->end_ptr + cache->size - (void *)s_head >= 1 << PAGE_SHIFT) {
            s_head->was_full = 1;
            list_add_tail(&(current_page->list), &(cache->node.full));

#ifdef SLAB_DEBUG
            kernel_printf("Become full\n");
#endif  // ! SLAB_DEBUG
        }
#ifdef SLAB_DEBUG
        kernel_printf("Page not full\nnr_objs:%d\tobject:%x\tend_ptr:%x\n", 
        s_head->nr_objs, object, s_head->end_ptr);
#endif  // ! SLAB_DEBUG
        return object;
    }

PageFull:
#ifdef SLAB_DEBUG
    kernel_printf("Page full\n");
#endif  // ! SLAB_DEBUG

    if (list_empty(&(cache->node.partial))) {
        // no partial pages
        // call the buddy system to allocate one more page to be slab-cache
        current_page = __alloc_pages(0);  // bplevel = 0 page === one page
        if (!current_page) {
            // allocate failed, memory in system is used up
            kernel_printf("ERROR: slab request one page in cache failed\n");
            while (1)
                ;
        }
#ifdef SLAB_DEBUG
        kernel_printf("\tnew page, index: %x \n", current_page - pages);
#endif  // ! SLAB_DEBUG

        // use standard format to shape the new-allocated page,
        // set the new page to be cpu.page then go back to PageNotFull
        format_slabpage(cache, current_page);
        s_head = (struct slab_head *)KMEM_ADDR(current_page, pages);
        goto PageNotFull;
    }

    // get a partial page
    #ifdef SLAB_DEBUG
    kernel_printf("Get partial page\n");
    #endif
    cache->cpu.page = container_of(cache->node.partial.next, struct page, list);
    current_page = cache->cpu.page;
    list_del(cache->node.partial.next);
    s_head = (struct slab_head *)KMEM_ADDR(current_page, pages);
    goto FromFreeList;
}

void slab_free(struct kmem_cache *cache, void * object) {
    struct page *opage = pages + ((unsigned int)object >> PAGE_SHIFT);
    unsigned int *ptr;
    struct slab_head *s_head = (struct slab_head *)KMEM_ADDR(opage, pages);
    unsigned char full;

    if (!s_head->nr_objs) {
        kernel_printf("ERROR: slab free error!\n");
        while (1)
            ;
    }
    object = (void *)((unsigned int)object | KERNEL_ENTRY);

#ifdef SLAB_DEBUG
    kernel_printf("page addr:%x\nobject:%x\nslabp:%x\n",
    opage, object, opage->slabp);
#endif  // ! SLAB_DEBUG

    full = (!opage->slabp) && s_head->was_full;
    *(unsigned int *)object = opage->slabp;
    opage->slabp = (unsigned int)object;
    s_head->nr_objs--;

#ifdef SLAB_DEBUG
    kernel_printf("nr_objs:%d\tslabp:%x\n", s_head->nr_objs, opage->slabp);
#endif  // ! SLAB_DEBUG

    if (list_empty(&(opage->list)))
        // cpu
        return;

#ifdef SLAB_DEBUG
    kernel_printf("Not CPU\n");
#endif  // ! SLAB_DEBUG

    if (!(s_head->nr_objs)) {
        list_del_init(&(opage->list));
        __free_pages(opage, 0);
        return;
#ifdef SLAB_DEBUG
        kernel_printf("Free\n");
#endif  // ! SLAB_DEBUG
    }

    if (full) {
        list_del_init(&(opage->list));
        list_add_tail(&(opage->list), &(cache->node.partial));
    }
}

// find the best-fit slab
unsigned int get_slab(unsigned int size) {
    unsigned int itop = PAGE_SHIFT;
    unsigned int index;
    unsigned int bf_num = (1 << (PAGE_SHIFT - 1));  // half page
    unsigned int bf_index = PAGE_SHIFT;             // record the best fit num & index

    for (index = 0; index < itop; index++) {
        if ((kmalloc_caches[index].objsize >= size) && (kmalloc_caches[index].objsize < bf_num)) {
            bf_num = kmalloc_caches[index].objsize;
            bf_index = index;
        }
    }

    return bf_index;
}

void *kmalloc(unsigned int size) {
    void *result;

    if (!size)
        return 0;

    result = phy_kmalloc(size);

    if (result)
        return (void *)(KERNEL_ENTRY | (unsigned int)result);
    else
        return 0;
}

void *phy_kmalloc(unsigned int size) {
    struct kmem_cache *cache;
    unsigned int bf_index;

    if (!size)
        return 0;

    // if the size larger than the max size of slab system, then call buddy to
    // solve this
    if (size > kmalloc_caches[PAGE_SHIFT - 1].objsize) {
        size += (1 << PAGE_SHIFT) - 1;
        size &= ~((1 << PAGE_SHIFT) - 1);
        return alloc_pages(size >> PAGE_SHIFT);
    }

    bf_index = get_slab(size);
    if (bf_index >= PAGE_SHIFT) {
        kernel_printf("ERROR: No available slab\n");
        while (1)
            ;
    }
    return slab_alloc(&(kmalloc_caches[bf_index]));
}

void kfree(void *obj) {
    struct page *page;

    obj = (void *)((unsigned int)obj & (~KERNEL_ENTRY));
    page = pages + ((unsigned int)obj >> PAGE_SHIFT);
    if (!(page->flag == BUDDY_SLAB))
        free_pages((void *)((unsigned int)obj & ~((1 << PAGE_SHIFT) - 1)), page->bplevel);

    slab_free(page->virtual, obj);
}