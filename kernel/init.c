#include <arch.h>
#include <driver/ps2.h>
#include <driver/vga.h>
#include <exc.h>
#include <intr.h>
#include <page.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/fs/fat.h>
#include <zjunix/log.h>
#include <zjunix/pc.h>
#include <zjunix/slab.h>
#include <zjunix/syscall.h>
#include <zjunix/time.h>
#include "../usr/ps.h"

// int init_done = 0;

void machine_info() {
    int row;
    int col;
    kernel_printf("\n%s\n", "Renaissance OS");
    row = cursor_row;
    col = cursor_col;
    cursor_row = 29;
    kernel_printf("%s", "Created by Yuan, Liu, Cui, Zhejiang University.");
    cursor_row = row;
    cursor_col = col;
    kernel_set_cursor();
}

#pragma GCC push_options
#pragma GCC optimize("O0")
void create_startup_process() {
    kernel_printf("Creating startup process...\n");
    int res;
    res = pc_create("shell", (void*)ps, 1, 0, 0);
    if(res != 1)
    kernel_printf("shell created fail!\n");
    else
    kernel_printf("shell created success!\n");
}
#pragma GCC pop_options

void init_kernel() {
    // init_done = 0;
    kernel_clear_screen(31);
    // Exception
    init_exception();
    // Page table
    init_pgtable();
    // Drivers
    init_vga();
    init_ps2();
    // Memory management
    log(LOG_START, "Memory Modules.");
    init_bootmm();
    log(LOG_OK, "Bootmem.");
    init_buddy();
    log(LOG_OK, "Buddy.");
    init_slab();
    log(LOG_OK, "Slab.");
    log(LOG_END, "Memory Modules.");
    // File system
    log(LOG_START, "File System.");
    init_fs();
    log(LOG_END, "File System.");
// #ifdef VFS_DEBUG
//     // Virtual file system
//     log(LOG_START, "Virtual file System.");
//     if(!init_vfs())
//         log(LOG_END, "Virtual file System.");
//     else
//         log(LOG_FAIL, "Virtual file System.");
// #endif
    // System call
    log(LOG_START, "System Calls.");
    init_syscall();
    log(LOG_END, "System Calls.");

    // Process control
    log(LOG_START, "PID Module.");
    init_pid();
    log(LOG_END, "PID Module.");
    log(LOG_START, "Process Control Module.");
    init_pc();
    create_startup_process();
    log(LOG_END, "Process Control Module.");
    // Interrupts
    log(LOG_START, "Enable Interrupts.");
    init_interrupts();
    log(LOG_END, "Enable Interrupts.");
    // Init finished
    machine_info();
    // init_done = 1;
    *GPIO_SEG = 0x11223344;
    // Enter shell
    while (1)
        ;
}
