#ifndef _ZJUNIX_SLAB_H
#define _ZJUNIX_SLAB_H

#include <zjunix/list.h>
#include <zjunix/buddy.h>

#define SIZE_INT 4
#define SLAB_AVAILABLE 0x0
#define SLAB_USED 0xff

// slab_head makes the allocation accessible from end_ptr to the end of the page
struct slab_head {
    void *end_ptr;          // points to the head of the rest of the page
    unsigned int nr_objs;   // keeps the numbers of memory segments that has been allocated
    char was_full;          // indicates whether the page was full
};

// slab pages is chained in this struct
struct kmem_cache_node {
    struct list_head partial;   // keeps the list of untotally-allocated pages
    struct list_head full;      // keeps the list of totally-allocated pages
};

// current allocated page
struct kmem_cache_cpu {
    struct page *page;
};

struct kmem_cache {
    unsigned int size;
    unsigned int objsize;
    unsigned int offset;
    struct kmem_cache_node node;
    struct kmem_cache_cpu cpu;
    unsigned char name[16];
};

// external functions
extern void init_slab();
extern void *kmalloc(unsigned int size);
extern void *phy_kmalloc(unsigned int size);
extern void kfree(void *obj);

#endif