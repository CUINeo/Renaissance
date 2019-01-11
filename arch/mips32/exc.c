#include "exc.h"

#include <driver/vga.h>
#include <zjunix/pc.h>
#include <zjunix/vm.h>
#include <zjunix/utils.h>
#include <zjunix/slab.h>
#include <zjunix/page.h>
#include <driver/ps2.h>

#pragma GCC push_options
#pragma GCC optimize("O0")

exc_fn exceptions[32];
int count = 0;
int count_2 = 0;

void tlb_refill(unsigned int bad_addr) {
    pgd_t* pgd;
    unsigned int pde_index, pte_index, pte_near_index;
    unsigned int pde, pte, pte_near;
    unsigned int pte_phy, pte_near_phy;
    unsigned int* pde_ptr, pte_ptr;
    unsigned int entry_lo0, entry_lo1, entry_hi;

#ifdef TLB_DEBUG
    unsigned int entry_hi_test;
    asm volatile(
        "mfc0 $t0, $10\n\t"
        "move %0, $t0\n\t"
        : "=r"(entry_hi_test)
        );

    kernel_printf("bad_addr = %x    entry_hi = %x\n", bad_addr, entry_hi_test);
    kernel_printf("running_task: %x pid: %d\n", running_task, running_task->pid);
#endif

    if (running_task->mm == 0) {
    	// struct mm_struct *mm = mm_create();
    	// running_task->mm = mm;
    	
        // kernel_printf("tlb_refill: mm is full!  pid: %d\n", running_task->pid);
        // goto ERROR;
    }

    pgd = running_task->mm->pgd;
    if (pgd == 0) {
        // pgd doesn't exist
        // kernel_printf("tlb_refill: pgd doesn't exist\n");
        // goto ERROR;
    }

    // page align
    bad_addr &= PAGE_MASK;
    
    pde_index = bad_addr >> PGD_SHIFT;
    pde = pgd[pde_index];
    pde &= PAGE_MASK;

    if (pde == 0) { //二级页表不存在
        pde = (unsigned int) kmalloc(PAGE_SIZE);

#ifdef TLB_DEBUG
        kernel_printf("tlb_refill: secondary page table doesn't exist!\n");
#endif

        if (pde == 0) {
            kernel_printf("tlb_refill: secondary page table alloc fail!\n");
            goto ERROR;
        }

        kernel_memset((void*)pde, 0, PAGE_SIZE);
        pgd[pde_index] = pde;
        pgd[pde_index] &= PAGE_MASK;
        pgd[pde_index] |= 0x0f; //attr
    }

#ifdef  VMA_DEBUG
    kernel_printf("tlb_refill: secondary page table address: %x\n", pde_addr);
#endif
    
    pde_ptr = (unsigned int*)pde;
    pte_index = (bad_addr >> PAGE_SHIFT) & INDEX_MASK;
    pte = pde_ptr[pte_index];
    pte &= PAGE_MASK;

    if (pte == 0) {
        // alloc page
#ifdef TLB_DEBUG
    kernel_printf("page not exist\n");
#endif
        pte = (unsigned int)kmalloc(PAGE_SIZE);

        if (pte == 0) {
            // kernel_printf("tlb_refill: page alloc fail!\n");
            // goto ERROR;
        }

        kernel_memset((void*)pte, 0, PAGE_SIZE);
        pde_ptr[pte_index] = pte;
        pde_ptr[pte_index] &= PAGE_MASK;
        pde_ptr[pte_index] |= 0x0f;
    }

    pte_near_index = pte_index ^ 0x01;
    pte_near = pde_ptr[pte_near_index];
    pte_near &= PAGE_MASK;

#ifdef  VMA_DEBUG
    kernel_printf("pte: %x  pte_index: %x  pte_near_index: %x\n", pte, pte_index, pte_near_index);
#endif

    if (pte_near == 0) {
#ifdef TLB_DEBUG
        kernel_printf("near page doesn't exist!\n");
#endif
        pte_near = (unsigned int)kmalloc(PAGE_SIZE);

        if (pte_near == 0) {
            // kernel_printf("tlb_refill: alloc pte_near_addr fail!\n");
            // goto ERROR;
        }

        kernel_memset((void*)pte_near, 0, PAGE_SIZE);
        pde_ptr[pte_near_index] = pte_near;
        pde_ptr[pte_near_index] &= PAGE_MASK;
        pde_ptr[pte_near_index] |= 0x0f;
    } 

    // get physical address of pte and pte_near
    pte_phy = pte - 0x80000000;
    pte_near_phy = pte_near - 0x80000000;

#ifdef TLB_DEBUG
    kernel_printf("pte_phy: %x, pte_near_phy: %x\n", pte_phy, pte_near_phy);
#endif
    
    if (pte_index & 0x01 == 0) {
        // even
        entry_lo0 = (pte_phy >> 12) << 6;
        entry_lo1 = (pte_near_phy >> 12) << 6;
    }
    else {
        // odd
        entry_lo0 = (pte_near_phy >> 12) << 6;
        entry_lo1 = (pte_near >> 12) << 6;
    }

    entry_lo0 |= (3 << 3);
    entry_lo1 |= (3 << 3);
    entry_lo0 |= 0x06;
    entry_lo1 |= 0x06;

    entry_hi = (bad_addr & PAGE_MASK) & (~(1 << PAGE_SHIFT));
    entry_hi |= running_task->ASID;
    
#ifdef TLB_DEBUG
    kernel_printf("tlb_refill: pid: %d\n", running_task->pid);
    kernel_printf("tlb_refill: entry_hi: %x, entry_lo0: %x, entry_lo1: %x\n", entry_hi, entry_lo0, entry_lo1);
#endif

    asm volatile (
        "move $t0, %0\n\t"
        "move $t1, %1\n\t"
        "move $t2, %2\n\t"
        "mtc0 $t0, $10\n\t"
        "mtc0 $zero, $5\n\t"
        "mtc0 $t1, $2\n\t"
        "mtc0 $t2, $3\n\t"
        "nop\n\t"
        "nop\n\t"
        "tlbwr\n\t"
        "nop\n\t"
        "nop\n\t"
        :
        : "r"(entry_hi),
          "r"(entry_lo0),
          "r"(entry_lo1)
    );

    unsigned int* pgd_ = running_task->mm->pgd;
    unsigned int pde_, pte_;
    unsigned int* pde_ptr_;
    int i, j;
    count_2 ++;
    
    for (i = 0; i < 1024; i++) {
        pde_ = pgd_[i];
        pde_ &= PAGE_MASK;
       
        if (pde_ == 0)
            continue;
        // kernel_printf("pde: %x\n", pde_);
        pde_ptr_ = (unsigned int*)pde_;
        for (j = 0; j < 1024; j++) {
            pte_ = pde_ptr_[j];
            pte_ &= PAGE_MASK;
            // if (pte_ != 0) {
                // kernel_printf("\tpte: %x\n", pte_);
            // }
        }
    }

    return;

ERROR:
    while(1)
        ;
}

void do_exceptions(unsigned int status, unsigned int cause, context* pt_context, unsigned int bad_addr) {
    int index = cause >> 2;
    index &= 0x1f;
    
    #ifdef  TLB_DEBUG
    unsigned int count;
    #endif

    if (index == 2 || index == 3) {
        // tlb_refill(bad_addr);
        #ifdef TLB_DEBUG
        kernel_printf("refill done\n");

        //count = 0x
       // kernel_getchar();
        #endif
        return ;
    }

    if (exceptions[index]) {
        exceptions[index](status, cause, pt_context);
    } else {
        struct task_struct* pcb;
        unsigned int badVaddr;
        asm volatile("mfc0 %0, $8\n\t" : "=r"(badVaddr));
        //modified by Ice
        pcb = running_task;
        kernel_printf("\nProcess %s exited due to exception cause=%x;\n", pcb->name, cause);
        kernel_printf("status=%x, EPC=%x, BadVaddr=%x\n", status, pcb->context.epc, badVaddr);
    //    pc_kill_syscall(status, cause, pt_context);
            //Done by Ice
        while (1)
            ;
    }
}

void register_exception_handler(int index, exc_fn fn) {
    index &= 31;
    exceptions[index] = fn;
}

void init_exception() {
    // status 0000 0000 0000 0000 0000 0000 0000 0000
    // cause 0000 0000 1000 0000 0000 0000 0000 0000
    asm volatile(
        "mtc0 $zero, $12\n\t"
        "li $t0, 0x800000\n\t"
        "mtc0 $t0, $13\n\t");
}

#pragma GCC pop_options