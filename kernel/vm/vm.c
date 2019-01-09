#include "vm.h"
#include <zjunix/vm.h>
#include <zjunix/slab.h>
#include <zjunix/utils.h>
#include <zjunix/pc.h>
#include <driver/vga.h>
#include <arch.h>

/* ------------- mm_struct ------------- */
// Create a mm struct
struct mm_struct *mm_create()
{
    // alloc spaces for mm
    struct mm_struct *mm;
    mm = kmalloc(sizeof(*mm));

#ifdef VMA_DEBUG
    kernel_printf("creating mm struct: %x\n", mm);
#endif

    if (mm == 0) {
        // alloc fail
#ifdef VMA_DEBUG
        kernel_printf("create fail", mm);
#endif
        return 0;
    }
    else {
        // alloc success
        kernel_memset(mm, 0, sizeof(*mm));
        // alloc spaces for pgd
        mm->pgd = kmalloc(PAGE_SIZE);
        if (mm->pgd == 0) {
            // alloc fail
#ifdef VMA_DEBUG
            kernel_printf("create fail", mm);
#endif
            kfree(mm);
            return 0;
        }
        else {
            // alloc success
#ifdef VMA_DEBUG
            kernel_printf("create success", mm);
#endif
            kernel_memset(mm->pgd, 0, PAGE_SIZE);
            return mm;
        }
    }
}

// Delete a mm struct
void mm_delete(struct mm_struct *mm)
{
#ifdef VMA_DEBUG
    kernel_printf("deleting mm struct: %x\n", mm);
#endif
    pgd_delete(mm->pgd);
    exit_mmap(mm);
    kfree(mm);
#ifdef VMA_DEBUG
    kernel_printf("delete success", mm);
#endif
}

// Delete page directory recursively
void pgd_delete(pgd_t *pgd)
{
    int i, j;
    unsigned int pde_addr, pte_addr;
    unsigned int *pde;

    // Traversal page directory
    for (i = 0; i < ENTRY_PER_PAGE; i++) {
        // Get each secondary page table
        pde_addr = *(pgd + i);
        pde_addr &= PAGE_MASK;

        if (pde_addr == 0)
            // Secondary page table doesn't exist
            continue;
        else {
            // Secondary page table exists
            pde = (unsigned int *)pde_addr;

            // Traversal secondary page table
            for (j = 0; j < ENTRY_PER_PAGE; j++) {
                pte_addr = *(pde + j);
                pte_addr &= PAGE_MASK;

                if (pte_addr == 0)
                    // Page doesn't exist
                    continue;
                else {
#ifdef VMA_DEBUG
                    kernel_printf("delete pte: %x\n", pte_addr);
#endif                    
                    kfree((void *)pte_addr);
                }
            }
#ifdef VMA_DEBUG
            kernel_printf("delete pde: %x\n", pde_addr);
#endif
        }

#ifdef VMA_DEBUG
        kernel_printf("delete pgd: %x\n", pgd);
#endif
        kfree(pgd);
#ifdef VMA_DEBUG
        kernel_printf("pgd deleted");
#endif
        return;
    }
}

/* ------------- vma_struct ------------- */
// Find the first VMA with ending address greater than addr
struct vma_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
    struct vma_struct *vma = 0;

    if (mm) {
        // Get the latest used VMA
        vma = mm->mmap_cache;
        if (vma && vma->vm_end > addr && vma->vm_start >= addr) {
#ifdef VMA_DEBUG
            kernel_printf("find vma result: mmap_cache.");
#endif
            return vma;
        }

        vma = mm->mmap;
        while (vma) {
            if (vma && vma->vm_end > addr) {
                mm->mmap_cache = vma;
                break;
            }

            // Increment vma
            vma = vma->vm_next;
        }
    }

    return vma;
}

// Find the first vma overlapped with start addr~end addr
struct vma_struct *find_vma_intersection(struct mm_struct *mm, unsigned long start_addr, unsigned long end_addr)
{
    // Find the first vma with ending address greater than start_addr
    struct vma_struct *vma = find_vma(mm, start_addr);

    if (vma && end_addr <= vma->vm_end)
        vma = 0;

    return vma;
}

// Find the first vma with ending address greater than addr, recording the previous vma
struct vma_struct *find_vma_and_prev(struct mm_struct *mm, unsigned long addr, struct vma_struct** prev)
{
    struct vma_struct *vma = 0;
    *prev = 0;

    if (mm) {
        vma = mm->mmap;
        while (vma) {
            if (vma->vm_end > addr) {
                mm->mmap_cache = vma;
                break;
            }
            *prev = vma;
            vma = vma->vm_next;
        }
    }

    return vma;
}

// Get unmapped(not in vma) area(length is len) after addr
unsigned long get_unmapped_area(unsigned long addr, unsigned long len, unsigned long flags)
{
    // Get current mm struct
    struct mm_struct *mm = running_task->mm;
    struct vma_struct *vma;

    // Up allign addr and len to page size
    addr += PAGE_SIZE - 1;
    addr &= ~(PAGE_SIZE - 1);
    len += PAGE_SIZE - 1;
    len &= ~(PAGE_SIZE - 1);

    if (addr + len > KERNEL_ENTRY)
        // not enough space
        return -1;

    if (addr && addr + len <= KERNEL_ENTRY) {
        vma = find_vma(mm, addr);
        while (1) {
            if (addr + len > KERNEL_ENTRY)
                return -1;
            if (!vma || addr + len <= vma->vm_start)
                return addr;

            addr = vma->vm_end;
            vma = vma->vm_next;
        }
    }
}

// Find vma preceding addr, assisting insertion
struct vma_struct *find_vma_prepare(struct mm_struct *mm, unsigned long addr)
{
    struct vma_struct *vma, *ret;
    vma = mm->mmap;
    ret = 0;

    while (vma) {
        if (addr < vma->vm_start)
            // Current vma is the first vma that vm_start > addr
            break;
        
        ret = vma;
        vma = vma->vm_next;
    }

    return ret;
}

// Insert vma to the linked list
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *area)
{
    // Find preceding vma
    struct vma_struct *vma = find_vma_prepare(mm, area->vm_start);

#ifdef VMA_DEBUG
    kernel_printf("insert after: %x", vma);
#endif

    if (!vma) {
        // No preceding vma
#ifdef VMA_DEBUG
        kernel_printf("insert to the first position", vma, area->vm_start);
#endif
        area->vm_next = mm->mmap;
        mm->mmap = area;
    }
    else {
        area->vm_next = vma->vm_next;
        vma->vm_next = area;
    }
    // Increment the map count
    mm->map_count++;
}

// Check whether an address is in any vma
int is_in_vma(unsigned long addr)
{
    struct vma_struct *vma = running_task->mm->mmap;
    while (vma) {
        if (addr < vma->vm_start)
            return 0;
        if (addr >= vma->vm_start && addr <= vma->vm_end)
            return 1;
        vma = vma->vm_next;
    }
    return 0;
}

// Map a region
unsigned long do_map(unsigned long addr, unsigned long len, unsigned long flags)
{
    struct mm_struct *mm = running_task->mm;
    addr = get_unmapped_area(addr, len, flags);

    // Up allign len to page size
    len += PAGE_SIZE - 1;
    len &= ~(PAGE_SIZE - 1);

    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct));
    if (!vma)
        // Alloc fail
        return -1;

    vma->vm_mm = mm;
    vma->vm_start = addr;
    vma->vm_end = addr + len;

#ifdef VMA_DEBUG
    kernel_printf("start: %x    end: %x\n", vma->vm_start, vma->vm_end);
#endif

    insert_vma_struct(mm, vma);
    return addr;
}

// Unmap a region
int do_unmap(unsigned long addr, unsigned long len)
{
    int delete_count = 0;
    struct mm_struct *mm = running_task->mm;
    struct vma_struct *vma, *prev, *temp;

    if (addr + len > KERNEL_ENTRY)
        // Invalid address
        return -1;
    
    vma = find_vma_and_prev(mm, addr, &prev);
    if (!vma || vma->vm_start > addr + len)
        // The region has not been mapped
        return 0;
    if (vma->vm_end >= addr + len) {
        // Only need to delete vma
        if (!prev)
            mm->mmap = vma->vm_next;
        else
            prev->vm_next = vma->vm_next;
        kfree(vma);
        mm->map_count--;
        delete_count++;
    }
    else {
        while (1) {
            temp = vma;
            vma = vma->vm_next;
            kfree(temp);
            mm->map_count--;
            delete_count++;
            if (vma->vm_start > addr + len)
                break;
        }
    }
#ifdef VMA_DEBUG
    kernel_printf("%d vma(s) deleted, %d vma(s) remained\n", delete_count, mm->map_count);
#endif
    return 0;
}

// Free all vmas
void exit_mmap(struct mm_struct *mm)
{
    struct vma_struct *vma, *temp;
    vma = mm->mmap;
    
    mm->mmap_cache = 0;
    while (vma) {
        temp = vma;
        vma = vma->vm_next;
        kfree(temp);
        mm->map_count--;
    }

    // Error
    if (mm->map_count) {
        kernel_printf("map_count error.");
        mm->map_count = 0;
    }
}
