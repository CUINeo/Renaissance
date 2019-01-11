#include <driver/ps2.h>
#include <driver/sd.h>
#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/fs/fat.h>
#include <zjunix/slab.h>
#include <zjunix/time.h>
#include <zjunix/utils.h>
#include <zjunix/pc.h>
#include "exec.h"
#include "myvi.h"
#include "ps.h"
#include "cd.h"
#include "ls.h"

char ps_buffer[64];
char cur_path[256];
int ps_buffer_index;

void test_proc() {
    unsigned int timestamp;
    unsigned int currTime;
    unsigned int data;
    asm volatile("mfc0 %0, $9, 6\n\t" : "=r"(timestamp));
    data = timestamp & 0xff;
    while (1) {
        asm volatile("mfc0 %0, $9, 6\n\t" : "=r"(currTime));
        if (currTime - timestamp > 100000000) {
            timestamp += 100000000;
            *((unsigned int *)0xbfc09018) = data;
        }
    }
}

/* physical memory test functions */
void small_block_test() {
    unsigned int *addr;

    kernel_puts("small memory block test:\n", 0xff, 0);

    kernel_printf("\tsize: 1KB:\n");
    addr = kmalloc(1024);
    kernel_printf("\taddress: %x\n", addr);
    
    kernel_printf("\tsize: 4KB:\n");
    addr = kmalloc(4096);
    kernel_printf("\taddress: %x\n", addr);

    kernel_printf("\n");
}

void big_block_test() {
    unsigned int *addr;

    kernel_puts("big memory block test:\n", 0xff, 0);

    kernel_printf("\tsize: 8KB\n");
    addr = kmalloc(8192);
    kernel_printf("\taddress: %x\n", addr);

    kernel_printf("\tsize: 16KB\n");
    addr = kmalloc(16384);
    kernel_printf("\taddress: %x\n", addr);

    kernel_printf("\tsize: 32KB\n");
    addr = kmalloc(32768);
    kernel_printf("\taddress: %x\n", addr);

    kernel_printf("\n");
}

int str2int(char *param) {
    int i, ret;
    for (i = 0, ret = 0; param[i]; i++) {
        ret = ret * 10 + param[i] - 48;
    }

    return ret;
}

void buddy_test(char *param) {
    kernel_puts("buddy system test:\n", 0xff, 0);

    unsigned int bytes = str2int(param);
    unsigned int remainder = (bytes % 4096 == 0) ? 0 : 1;
    unsigned int pages = bytes / 4096 + remainder;
    unsigned int bplevel = 0;

    if (!pages) {
        // empty alloc
        kernel_printf("empty alloc!\n");
        return ;
    }
    else {
        // get bplevel
        bplevel = 1;
        while (1 << bplevel < pages)
            bplevel++;
    }

    kernel_printf("memory size: %d  pages required: %d  buddy level: %d\n", bytes, pages, bplevel);

    struct page *pbpage = test_alloc_pages(bplevel);
    test_free_pages(pbpage, bplevel);

    kernel_printf("\n");
}

void slab_test() {
    kernel_puts("allocate 5 memory blocks of size 100B.\n", 0xff, 0);

    int i;
    unsigned int *blocks[5];
    for (i = 0; i < 5; i++) {
        blocks[i] = kmalloc(100);
    }

    for (i = 0; i < 5; i++) {
        kfree(blocks[i]);
    }

    kernel_printf("\n");
}

void large_slab_test() {
    kernel_puts("allocate 10 memory blocks of size 100B.\n", 0xff, 0);

    int i;
    unsigned int *blocks[10];
    for (i = 0; i < 10; i++) {
        blocks[i] = kmalloc(100);
    }

    for (i = 0; i < 10; i++) {
        kfree(blocks[i]);
    }

    kernel_printf("\n");
}

// void slab_memory_test() {
//     kernel_puts("allocate 10 memory blocks of size 4096B.\n", 0xff, 0);

//     kernel_printf("before allocating\n");
//     buddy_info();

//     int i;
//     unsigned int *blocks[10];
//     for (i = 0; i < 10; i++) {
//         blocks[i] = kmalloc(4096);
//     }

//     kernel_printf("allocated\n");
//     buddy_info();

//     for (i = 0; i < 100; i++) {
//         kfree(blocks[i]);
//     }

//     kernel_printf("\n");
// }

void virtual_memory_test() {
    struct mm_struct* mm = mm_create();
    kernel_printf("mm struct created, address: %x\n", mm);

    unsigned int addr = 0;
    unsigned long len = 8192;
    // do map
    addr = do_map(addr, len, 0);
    kernel_printf("\n");

    // // check whether in vma
    // if (is_in_vma(addr + 4096)) {
    //     kernel_printf("%x is in vma!\n", addr + 4096);
    // }

    // do unmap
    do_unmap(addr, len);
    kernel_printf("\n");

    // delete mm
    mm_delete(mm);
    kernel_printf("\n");

    kernel_printf("Test success!\n");
    kernel_printf("\n");
}

void ps() {
    kernel_printf("Press any key to enter shell.\n");
    kernel_getchar();
    char c;
    int i;
    ps_buffer_index = 0;
    ps_buffer[0] = 0;
    for (i = 0; i < 256; i++)
    {
        cur_path[i] = 0;
    }
    cur_path[0] = '/';
    kernel_clear_screen(31); 
    kernel_puts("PowerShell\n", 0xfff, 0);
    kernel_puts("PS>> ", 0xfff, 0);
    while (1) {
        c = kernel_getchar();
        if (c == '\n') {
            ps_buffer[ps_buffer_index] = 0;
            if (kernel_strcmp(ps_buffer, "exit") == 0) {
                ps_buffer_index = 0;
                ps_buffer[0] = 0;
                kernel_printf("\nPowerShell exit.\n");
            } else
                parse_cmd();
            ps_buffer_index = 0;
            kernel_puts("PS>> ", 0xfff, 0);
        } else if (c == 0x08) {
            if (ps_buffer_index) {
                ps_buffer_index--;
                kernel_putchar_at(' ', 0xfff, 0, cursor_row, cursor_col - 1);
                cursor_col--;
                kernel_set_cursor();
            }
        } else {
            if (ps_buffer_index < 63) {
                ps_buffer[ps_buffer_index++] = c;
                kernel_putchar(c, 0xfff, 0);
            }
        }
    }
}

void parse_cmd() {
    unsigned int result = 0;
    char dir[32];
    char c;
    kernel_putchar('\n', 0, 0);
    char sd_buffer[8192];
    int i = 0;
    char *param;
    char *src;
    char *dest;
    char absolute[256];

    for (i = 0; i < 256; i++) {
        absolute[i] = 0;
    }

    for (i = 0; i < 63; i++) {
        if (ps_buffer[i] == ' ') {
            ps_buffer[i] = 0;
            break;
        }
    }
    if (i == 63)
        param = ps_buffer;
    else
        param = ps_buffer + i + 1;

    if (ps_buffer[0] == 0) {
        return;
    } 
    else if (kernel_strcmp(ps_buffer, "clear") == 0) {
        kernel_clear_screen(31);
    }
     else if (kernel_strcmp(ps_buffer, "echo") == 0) {
        kernel_printf("%s\n", param);
    } 
    else if (kernel_strcmp(ps_buffer, "gettime") == 0) {
        char buf[10];
        get_time(buf, sizeof(buf));
        kernel_printf("%s\n", buf);
    } 
    else if (kernel_strcmp(ps_buffer, "sdwi") == 0) {
        for (i = 0; i < 512; i++)
            sd_buffer[i] = i;
        sd_write_block(sd_buffer, 7, 1);
        kernel_puts("sdwi\n", 0xfff, 0);
    } 
    else if (kernel_strcmp(ps_buffer, "sdr") == 0) {
        sd_read_block(sd_buffer, 7, 1);
        for (i = 0; i < 512; i++) {
            kernel_printf("%d ", sd_buffer[i]);
        }
        kernel_putchar('\n', 0xfff, 0);
    } 
    else if (kernel_strcmp(ps_buffer, "sdwz") == 0) {
        for (i = 0; i < 512; i++) {
            sd_buffer[i] = 0;
        }
        sd_write_block(sd_buffer, 7, 1);
        kernel_puts("sdwz\n", 0xfff, 0);
    // } else if (kernel_strcmp(ps_buffer, "mminfo") == 0) {
    //     bootmap_info("bootmm");
    //     buddy_info();
    // } else if (kernel_strcmp(ps_buffer, "mmtest") == 0) {
    //     kernel_printf("kmalloc : %x, size = 1KB\n", kmalloc(1024));
    } 
    else if (kernel_strcmp(ps_buffer, "ps") == 0) {
        if(kernel_strcmp(param, "-ready") == 0)
             print_ready();
         else if(kernel_strcmp(param, "-running") == 0)
             print_running();
         else if(kernel_strcmp(param, "-waiting") == 0)
             print_waiting();
         else if(kernel_strcmp(param, "-exit") == 0)
             print_exited();
        print_proc();
    } 
    else if (kernel_strcmp(ps_buffer, "loop") == 0) {
        kernel_printf("Loop process has been created.\n");
        // background task
        pc_create_back("loop", (void*)loop, 4, 0, 0);
    }
    else if (kernel_strcmp(ps_buffer, "kill") == 0) {
        int pid = param[0] - '0';
        kernel_printf("Killing process %d\n", pid);
        result = pc_kill(pid);
        kernel_printf("kill return with %d\n", result);
    } 
    else if (kernel_strcmp(ps_buffer, "time") == 0) {
        unsigned int init_gp;
        // asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
        // pc_create(2, system_time_proc, (unsigned int)kmalloc(4096), init_gp, "time");
    } 
    else if (kernel_strcmp(ps_buffer, "proc") == 0) {
        kernel_printf("test proc:");
        result = kernel_exec(1, (void*)param);
        kernel_printf("proc return with %d\n", result);
    }
    else if(kernel_strcmp(ps_buffer, "proc&") == 0){
     kernel_printf("test proc back:");
        result = kernel_exec_back(1, (void*)param);
        kernel_printf("proc_back return with %d\n", result);
    }
    else if(kernel_strcmp(ps_buffer, "sched") == 0) {
        print_sched();
    }
    else if(kernel_strcmp(ps_buffer, "bg") == 0){
    int pid = param[0] - '0';
    kernel_printf("Moving Background PID: %d\n", pid);
        kernel_bg(pid);
    }
    else if(kernel_strcmp(ps_buffer, "fg") == 0){
    int pid = param[0] - '0';
       kernel_printf("Moving Front PID: %d\n", pid);
         kernel_fg(pid);
    }
    else if (kernel_strcmp(ps_buffer, "exec") == 0) {
        result = exec(param);
        kernel_printf("exec return with %d\n", result);
    } 
    else if (kernel_strcmp(ps_buffer, "bootmminfo") == 0) {
        bootmap_info();
        kernel_printf("\n");
    }
    else if (kernel_strcmp(ps_buffer, "buddyinfo") == 0) {
        buddy_info();
        kernel_printf("\n");
    } 
    else if (kernel_strcmp(ps_buffer, "mt") == 0) {
        small_block_test();
        big_block_test();
    } 
    else if (kernel_strcmp(ps_buffer, "bt") == 0) {
        buddy_test(param);
    } 
    else if (kernel_strcmp(ps_buffer, "st") == 0) {
        slab_test();
    }
    else if (kernel_strcmp(ps_buffer, "lst") == 0) {
        large_slab_test();
    }
    // else if (kernel_strcmp(ps_buffer, "smt") == 0) {
    //     slab_memory_test();
    // }
    else if (kernel_strcmp(ps_buffer, "vmt") == 0) {
        virtual_memory_test();
    }
    else if (kernel_strcmp(ps_buffer, "execvm") == 0) {
        kernel_printf("kernel_execvm:\n");
        kernel_execvm(1, (void*)param);
        kernel_printf("\n");
    }
    else if (kernel_strcmp(ps_buffer, "cat") == 0) {
        //result = fs_cat(param);
        if(cur_path[str_len(cur_path) - 1] == '/')
            combine(absolute, cur_path, param, 0);
        else
            combine(absolute, cur_path, param, '/');
        kernel_printf("cat path: %s\n", absolute);
        //result = vfsfile->fat32_file->cat(absolute);
        result = fs_cat(absolute);
        //kernel_printf("param %s\n", param);
        kernel_printf("sd_buffersd_buffercat return with %d\n", result);
    } 
    else if (kernel_strcmp(ps_buffer, "ls") == 0) {
        //kernel_printf("First: param: %s\n", param);
        result = ls(param);
       //kernel_printf("pwd: %s\n", cur_path);
        
        kernel_printf("ls return with %d\n", result);
    }
    else if (kernel_strcmp(ps_buffer, "vi") == 0) {
        result = myvi(param);
        kernel_printf("vi return with %d\n", result);
    }
    else if (kernel_strcmp(ps_buffer, "rm") == 0) {
        kernel_printf("In remove instruction\n");
        if(cur_path[str_len(cur_path) - 1] == '/')
            combine(absolute, cur_path, param, 0);
        else
            combine(absolute, cur_path, param, '/');
        //result = vfsfile->fat32_file->rm(absolute);
        result = fs_rm(absolute);
        kernel_printf("  rm return with %d\n", result);
    } 
    else if (kernel_strcmp(ps_buffer, "mkdir") == 0) {
        //result = vfsfile->fat32_file->mkdir(param);
        if(cur_path[str_len(cur_path) - 1] == '/')
            combine(absolute, cur_path, param, 0);
        else
            combine(absolute, cur_path, param, '/');
        result = fs_mkdir(absolute);
        kernel_printf("  mkdir return with %d\n", result);
    } 
    else if (kernel_strcmp(ps_buffer, "rmdir") == 0) {
        //result = vfsfile->fat32_file->rmdir(param);
        result = fs_rmdir(param);
        kernel_printf("  rmdir return with %d\n", result);
    } 
    else if (kernel_strcmp(ps_buffer, "touch") == 0) {
        if(cur_path[str_len(cur_path) - 1] == '/')
            combine(absolute, cur_path, param, 0);
        else
            combine(absolute, cur_path, param, '/');

        kernel_printf("touch path:%s \n", absolute);
        result = fs_touch(absolute);
        //result = vfsfile->fat32_file->touch(absolute);
        kernel_printf("  touch return with %d\n", result);
    } 
    else if (kernel_strcmp(ps_buffer, "pwd") == 0) {
        kernel_printf("  %s\n",cur_path);
    } 
    else if (kernel_strcmp(ps_buffer, "cd") == 0) {
        cd(param, cur_path);
    } 
    //absolute path
    else if (kernel_strcmp(ps_buffer, "mv") == 0) {
        for (i = 0; i < 63; i++)
        {
            if(param[i] == ' ')
            {
                param[i] = 0;
                break;
            }
        }
        if (i == 63)
        {
            src = param;
        }
        else
        {
            src = param;
            dest = param + i + 1;
        }
        //kernel_printf("%s\n", src);
        //kernel_printf("%s\n",dest);
        result = fs_mv(src, dest);
        kernel_printf("  mv return with %d\n", result);
    } 
    //absolute path
    else if (kernel_strcmp(ps_buffer, "cp") == 0) {
        /* add cp instruction */
        for(i = 0; i < 63; i++)
        {
            if(param[i] == ' ')
            {
                param[i] = 0;
                break;
            }
        }
        if (i == 63)
        {
            src = param;
        }
        else
        {
            src = param;
            dest = param + i + 1;
        }
        kernel_printf("src: %s\n", src);
        kernel_printf("dest: %s\n", dest);
        if (str_len(src) == str_len(dest)) {
            if (str_equal(src, dest, str_len(src)) == 1)
            {
                kernel_printf("cp cannot allowed.\n");
            }
        }
        else {
            result = fs_cp(src, dest);
            kernel_printf("  cp return with %d\n", result);
        }
    }

    else {
        kernel_puts(ps_buffer, 0xfff, 0);
        kernel_puts(": command not found\n", 0xfff, 0);
    }
}
