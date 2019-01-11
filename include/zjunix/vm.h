#ifndef _ZJUNIX_VM_H
#define _ZJUNIX_VM_H

#include <zjunix/page.h>
#include <zjunix/rbtree.h>

#define  USER_CODE_ENTRY    0x00100000
#define  USER_DATA_ENTRY    0x01000000
#define  USER_DATA_END
#define  USER_BRK_ENTRY     0x10000000
#define  USER_STACK_ENTRY   0x80000000
#define  USER_DEFAULT_ATTR  0x0f

// struct mm_struct {
//     struct vma_struct *mmap;        // vma linked list
//     struct vma_struct *mmap_cache;  // latest used vma

//     pgd_t *pgd;     // page directory pointer
//     int map_count;  // number of vmas
    
//     unsigned int start_code, end_code, start_data, end_data;
//     unsigned int brk, start_brk, start_stack;
// };

// struct vma_struct {
//     struct mm_struct *vm_mm;     // the address space we belong to
//     unsigned int vm_start;       // starting address within vm_mm
//     unsigned int vm_end;         // the first byte after our end address within vm_mm
//     struct vma_struct *vm_next;  // next vma
// };

struct mm_struct {
    struct rb_root *root;           // vma rbtree
    struct vma_struct *mmap_cache;  // latest used vma

    pgd_t *pgd;     // page directory pointer
    int map_count;  // number of vmas
    
    unsigned int start_code, end_code, start_data, end_data;
    unsigned int brk, start_brk, start_stack;
};

struct vma_struct {
    struct mm_struct *vm_mm;// the address space we belong to
    unsigned int vm_start;  // starting address within vm_mm
    unsigned int vm_end;    // the first byte after our end address within vm_mm
    struct rb_node *node;   // the node of rbtree
};

// mm_struct operations
struct mm_struct *mm_create();
void mm_delete(struct mm_struct *mm);

extern void set_tlb_asid(unsigned int asid);

int is_in_vma(unsigned long addr);
unsigned long do_map(unsigned long addr, unsigned long len, unsigned long flags);
int do_unmap(unsigned long addr, unsigned long len);

#endif