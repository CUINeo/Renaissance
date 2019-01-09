#ifndef _ZJUNIX_BUDDY_H
#define _ZJUNIX_BUDDY_H

#include <zjunix/list.h>
#include <zjunix/lock.h>

// 1000...
#define BUDDY_RESERVED (1 << 31)
// 0100...
#define BUDDY_ALLOCED (1 << 30)
// 0010...
#define BUDDY_SLAB (1 << 29)

/*
 * struct buddy page is one info-set for the buddy group of pages
 */
struct page {
    unsigned int flag;          // the declaration of the usage of the page
    unsigned int reference;     // useless for now
    struct list_head list;    // double-way listed list
    void *virtual;              // default 0x(-1)
    unsigned int bplevel;       // the order level of the page
    unsigned int slabp;         /* if the page is used in slab system.
                                 * then slabp represents the base address of free space
                                 */
};

#define PAGE_SHIFT 12
/*
 * order meands the size of the set of pages, e.g. order = 1 -> 2^1
 * pages(consequent) are free in current system, we allow the max order
 * to be 4(2^4 consequent free pages)
 */
#define MAX_BUDDY_ORDER 4

struct freelist {
    unsigned int nr_free;       // number of free blocks
    struct list_head free_head;  // linked list
};

struct buddy_sys {
    unsigned int buddy_start_pfn;                   // physical start address
    unsigned int buddy_end_pfn;                     // physical end address
    struct page *start_page;                        // start page pointer
    struct lock_t lock;                             // exclusive lock
    struct freelist freelist[MAX_BUDDY_ORDER + 1];  // 0, 1, 2, 3, 4
};

// macros
#define _is_same_bpgroup(page, bage) (((*(page)).bplevel == (*(bage)).bplevel))
#define _is_same_bplevel(page, lval) ((*(page)).bplevel == (lval))
#define set_bplevel(page, lval) ((*(page)).bplevel = (lval))
#define set_flag(page, val) ((*(page)).flag |= (val))
#define clean_flag(page, val) ((*(page)).flag &= ~(val))
#define has_flag(page, val) ((*(page)).flag & val)
#define set_ref(page, val) ((*(page)).reference = (val))
#define inc_ref(page, val) ((*(page)).reference += (val))
#define dec_ref(page, val) ((*(page)).reference -= (val))

// extern variables and functions
extern struct page *pages;
extern struct buddy_sys buddy;

// internal
extern void __free_pages(struct page *page, unsigned int order);
extern struct page *__alloc_pages(unsigned int order);

// external
extern void free_pages(void *addr, unsigned int order);
extern void *alloc_pages(unsigned int order);
extern void init_buddy();
extern void buddy_info();

// functions for test
extern struct page *test_alloc_pages(unsigned int bplevel);
extern void test_free_pages(struct page *pbpage, unsigned int bplevel);

#endif