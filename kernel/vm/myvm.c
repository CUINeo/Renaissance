#include "vm.h"
#include "rbtree.h"
#include <zjunix/vm.h>
#include <zjunix/slab.h>
#include <zjunix/utils.h>
#include <zjunix/pc.h>
#include <driver/vga.h>
#include <arch.h>

// #define VMA_DEBUG

/* ------------- mm_struct ------------- */
// Create a mm struct
struct mm_struct *mm_create()
{
    // alloc spaces for mm
    struct mm_struct *mm;
    mm = kmalloc(sizeof(*mm));

    kernel_printf("creating mm struct: %x\n", mm);

    if (mm == 0) {
        // alloc fail
        kernel_printf("create fail!\n");
        return 0;
    }
    else {
        // alloc success
        kernel_memset(mm, 0, sizeof(*mm));
        // alloc spaces for pgd
        mm->pgd = kmalloc(PAGE_SIZE);
        if (mm->pgd == 0) {
            // alloc fail
            kernel_printf("create fail\n");
            kfree(mm);
            return 0;
        }
        else {
            // alloc success
            kernel_memset(mm->pgd, 0, PAGE_SIZE);
            return mm;
        }
    }
}

// Delete a mm struct
void mm_delete(struct mm_struct *mm)
{
    kernel_printf("deleting mm struct: %x\n", mm);
    pgd_delete(mm->pgd);
    kernel_printf("pgd deleted!\n");
    exit_mmap(mm);
    kernel_printf("map exited!\n");
    kfree(mm);
    kernel_printf("delete success!\n");
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
        kernel_printf("pgd deleted\n");
#endif
        return;
    }
}

/* ------------- vma_struct ------------- */
// Find the first VMA with ending address greater than addr
struct vma_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
    struct vma_struct *vma;

    if (mm) {
        // Get the latest used VMA
        vma = mm->mmap_cache;
        if (vma && vma->vm_end > addr && vma->vm_start >= addr) {
#ifdef VMA_DEBUG
            kernel_printf("find vma result: mmap_cache.\n");
#endif
            return vma;
        }

        // Get the root of the rbtree
        struct rb_root *root = mm->root;

   	    // Empty tree
	    if (root == 0)
	    	return 0;

        struct rb_node *node = root->rb_node;

        // Traversal the tree to find required vma
        while (node) {
        	// Get the container struct pointer
        	vma = rb_entry(node, struct vma_struct, node);

        	if (vma->vm_end > addr) {
        		// Check whether previous node's vm_end <= addr
        		struct rb_node *temp = rb_prev(node);
        		struct vma_struct *vtemp = rb_entry(temp, struct vma_struct, node);

        		if (vtemp->vm_end <= addr) {
					// Set mmap_cache and break
					mm->mmap_cache = vma;
        			return vma;
        		}
        		else {
        			// Go to left subtree
        			node = node->rb_left;
        		}
        	}

        	// Go to right subtree
        	node = node->rb_right;
        }
    }

    return 0;
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
    struct vma_struct *vma;
    struct rb_node *node;
    *prev = 0;

    if (mm) {
        // Get the latest used VMA
        vma = mm->mmap_cache;
        if (vma && vma->vm_end > addr && vma->vm_start >= addr) {
        	// Set prev and return
        	node = vma->node;
        	node = rb_prev(node);
        	*prev = rb_entry(node, struct vma_struct, node);

            return vma;
        }

        // Get the root of the rbtree
        struct rb_root *root = mm->root;
        // Empty tree
	    if (root == 0)
	    	return 0;
        node = root->rb_node;

        // Traversal the tree to find required vma
        while (node) {
        	// Get the container struct pointer
        	vma = rb_entry(node, struct vma_struct, node);

        	if (vma->vm_end > addr) {
        		// Check whether previous node's vm_end <= addr
        		struct rb_node *temp = rb_prev(node);
        		struct vma_struct *vtemp = rb_entry(temp, struct vma_struct, node);

        		if (vtemp->vm_end <= addr) {
					// Set mmap_cache, prev and break
					mm->mmap_cache = vma;
        			return vma;
        		}
        		else {
        			// Go to left subtree
        			node = node->rb_left;
        		}
        	}

        	// Go to right subtree
        	node = node->rb_right;
        }
    }

    return 0;
}

// Get unmapped(not in vma) area(length is len) after addr
unsigned long get_unmapped_area(unsigned long addr, unsigned long len, unsigned long flags)
{
    // Get current mm struct
    struct mm_struct *mm = running_task->mm;

    struct vma_struct *vma;
    struct rb_node *node;

    // Up allign addr and len to page size
    addr += PAGE_SIZE - 1;
    addr &= ~(PAGE_SIZE - 1);
    len += PAGE_SIZE - 1;
    len &= ~(PAGE_SIZE - 1);

    if (addr + len > KERNEL_ENTRY)
        // Not enough space
        return -1;

    if (addr && addr + len <= KERNEL_ENTRY) {
    	// Find the first vma ending after addr
        vma = find_vma(mm, addr);
        node = vma->node;

        while (1) {
            if (addr + len > KERNEL_ENTRY)
                return -1;
            if (!vma || addr + len <= vma->vm_start)
                return addr;

            addr = vma->vm_end;

            // Get next node
            node = rb_next(node);
            vma = rb_entry(node, struct vma_struct, node);
        }
    }
}

// Find vma preceding addr, assisting insertion
struct vma_struct *find_vma_prepare(struct mm_struct *mm, unsigned long addr)
{
	if (mm) {
		struct vma_struct *vma;

	    // Get the root of the rbtree
	    struct rb_root *root = mm->root;
        // Empty tree
		if (root == 0)
			return 0;

	    struct rb_node *node = root->rb_node;

	    while (node) {
	    	vma = rb_entry(node, struct vma_struct, node);

	        if (addr >= vma->vm_start) {
	            // Go to left subtree
	            node = node->rb_right;
	        }
	        else {
	        	// Check whether previous node precedes addr
	        	struct rb_node *temp = rb_prev(node);
        		struct vma_struct *vtemp = rb_entry(temp, struct vma_struct, node);

        		if (addr >= vtemp->vm_start) {
        			// Current vma is the first vma that vm_start > addr
        			return vtemp;
        		}
        		else {
        			// Go to left subtree
        			node = node->rb_left;
        		}
	        }
	    }
	}

	return 0;
}

// Insert vma to the rbtree
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *area)
{
	if (mm->root == 0) {
		// The tree is empty
		mm->root = kmalloc(sizeof(struct rb_root));

		if (mm->root == 0) {
			// Alloc fail
			kernel_printf("rbtree alloc fail!\n");
			return;
		}

		kernel_memset(mm->root, 0, sizeof(struct rb_root));

		mm->root->rb_node = kmalloc(sizeof(struct rb_node));
		if (mm->root->rb_node == 0) {
			// Alloc fail
			kernel_printf("rbtree alloc fail!\n");
			return;
		}
		kernel_memset(mm->root->rb_node, 0, sizeof(struct rb_node));

		// Alloc success
		area->node = mm->root->rb_node;
		mm->map_count++;
		mm->mmap_cache = area;
		return;
	}

	struct vma_struct *vma;

	// Get the root of the rbtree
    struct rb_root *root = mm->root;
    struct rb_node **new = &(root->rb_node), *parent = NULL;

    while (*new) {
    	vma = rb_entry(*new, struct vma_struct, node);

    	parent = *new;
    	if (area->vm_start >= vma->vm_end) {
    		// Go to right subtree
    		new = &((*new)->rb_right);
    	}
    	else if (area->vm_end <= vma->vm_start) {
    		// Go to left subtree
    		new = &((*new)->rb_left);
    	}
    	else {
    		kernel_printf("overlapped insert!\n");
    		return;
    	}
    }

    // Add new node and rebalance rbtree
	rb_link_node(area->node, parent, new);
	rb_insert_color(area->node, root);

    // Increment the map count
    mm->map_count++;	
}

// Check whether an address is in any vma
int is_in_vma(unsigned long addr)
{
	return 1;
    struct vma_struct *vma;

    // Get the root of the rbtree
    struct rb_root *root = running_task->mm->root;
    
    // Empty tree
    if (root == 0)
    	return 0;

    struct rb_node *node = root->rb_node;

    while (node) {
    	vma = rb_entry(node, struct vma_struct, node);

    	if (addr < vma->vm_start) {
    		// Go to left subtree
    		node = node->rb_left;
    	}
    	else if (addr > vma->vm_end) {
    		// Go to right subtree
    		node = node->rb_right;
    	}
    	else
    		return 1;
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
    if (!vma) {
        // Alloc fail
        kernel_printf("alloc vma fail!\n");
        return -1;
    }
    kernel_memset(vma, 0, sizeof(struct vma_struct));

    vma->vm_mm = mm;
    vma->vm_start = addr;
    vma->vm_end = addr + len;

    kernel_printf("vm_start: %x, vm_end: %x\n", vma->vm_start, vma->vm_end);
    kernel_printf("map success!\n");

    insert_vma_struct(mm, vma);
    return addr;
}

// Unmap a region
int do_unmap(unsigned long addr, unsigned long len)
{
	int delete_count = 0;
    int end_addr = addr + len;
    struct mm_struct *mm = running_task->mm;

    if (end_addr > KERNEL_ENTRY) {
        // Invalid address
        kernel_printf("Invalid operation\n");
        return -1;
    }
    
    // Get the root of the rbtree
    struct rb_root *root = mm->root;

    struct rb_node *node;
    struct vma_struct *vma;

    // Traversal the rbtree to delete
    for (node = rb_first(root); node; node = rb_next(node)) {
    	// Get the container of the rbtree node
        vma = rb_entry(node, struct vma_struct, node);

        if ((vma->vm_start >= addr && vma->vm_start <= end_addr) || 
        	(vma->vm_end >= addr && vma->vm_end <= end_addr)) {
	        // Delete the node and free vma
        	rb_erase(vma->node, root);

	        kfree(vma);
	        mm->map_count--;
	        delete_count++;
	    }
    }

    // kernel_printf("%d vma(s) deleted, %d vma(s) remained\n", delete_count, mm->map_count);
    return 0;
}

// Free all vmas
void exit_mmap(struct mm_struct *mm)
{
    struct vma_struct *vma;

    // Get the root of the rbtree
    struct rb_root *root = mm->root;
    struct rb_node *node;

    // Delete mmap_cache
    mm->mmap_cache = 0;

    // Empty tree
    if (root == 0 || root->rb_node == 0)
    	return;

    // Traversal the rbtree to delete
    for (node = rb_first(root); node; node = rb_next(node)) {
    	// Get the container of the rbtree node
        vma = rb_entry(node, struct vma_struct, node);
        // Free vma
        kfree(vma);
        mm->map_count--;
    }

    // Error
    if (mm->map_count) {
        kernel_printf("map_count error.\n");
        while (1)
        	;
    }
}
