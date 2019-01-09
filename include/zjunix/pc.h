#ifndef _PC_H
#define _PC_H

#include <zjunix/list.h>
#include <zjunix/pid.h>
#include <zjunix/vm.h>
#include <zjunix/fs/fat.h>

#define MAX_PRIO 8 //优先队列最大级数
#define KERNEL_STACK_SIZE 4096
#define TASK_NAME_LEN 32

#define BASIC_TIME_QUANTUM 10

// 进程类型
#define TYPE_FRONT 0
#define TYPE_BACK 1

// 进程状态
#define STATE_READY 0
#define STATE_RUNNING 1
#define STATE_WAITING 2
#define STATE_EXITED 3

extern struct task_struct *running_task;

// 寄存器信息结构，主要用于进程调度时的进程切换

struct regs_context {
    unsigned int epc;
    unsigned int at;
    unsigned int v0, v1;
    unsigned int a0, a1, a2, a3;
    unsigned int t0, t1, t2, t3, t4, t5, t6, t7;
    unsigned int s0, s1, s2, s3, s4, s5, s6, s7;
    unsigned int t8, t9;
    unsigned int hi, lo;
    unsigned int gp, sp, fp, ra;
};

// 进程控制块PCB结构

struct task_struct{
    char name[TASK_NAME_LEN];
    int type;                      //进程类型
    pid_t pid;
    pid_t parent;
    
    unsigned int ASID;                 //进程地址空间id
    struct mm_struct *mm;  //进程地址空间指针
    FILE *task_files;                  //进程打开文件指针

    unsigned int static_priority; //静态优先级
    unsigned int avg_sleep;       //平均睡眠时间
    unsigned int counter;         //剩余时间片

    struct regs_context context;  //寄存器信息
    struct list_head sched_node; //调度链表结点

    int state;                    //进程状态信息
    struct list_head state_node;  //状态链表结点
    struct list_head tasks_node;
};



// 联合体结构，同时存储PCB和内核栈
union task_union{
    struct task_struct task;        //进程控制块
    unsigned char kernel_stack[KERNEL_STACK_SIZE];  //进程的内核栈
};
typedef struct regs_context context; // 上下文结构context

// 主要函数
void init_pc(); // 进程管理模块初始化
void init_lists(); // 进程链表初始化

int pc_create(char *task_name, void(*entry)(unsigned int argc, void *args), 
                int static_priority, unsigned int argc, void *args);
int pc_kill(pid_t pid);
void pc_exit();
extern void switch_ex(struct regs_context* regs); // for context switch in pc_exit

void pc_schedule(unsigned int status, unsigned int cause, context* pt_context);
void print_proc(); // 输出所有进程信息

int kernel_exec(unsigned int argc, void *args);

struct task_struct* get_curr_pcb();
void pc_kill_syscall(unsigned int status, unsigned int cause, context* pt_context);

void task_files_delete(struct task_struct* task);

// 在队列中加入进程
void add_tasks_queue(struct task_struct *task);
void add_ready_queue(struct task_struct *task);
void add_wait_queue(struct task_struct *task);
void add_exit_queue(struct task_struct *task);


// 在队列中移除进程
void remove_tasks_queue(struct task_struct *task);
void remove_ready_queue(struct task_struct *task);
void remove_wait_queue(struct task_struct *task);
void remove_exit_queue(struct task_struct *task);

static void copy_context(context* src, context* dest);
void print_pcb(struct task_struct *task);
struct task_struct* find_in_tasks(pid_t pid);
int prog(unsigned int argc, void *args);

// O(1)scheduler functions
void add_sched(struct task_struct *task, int priority);
void remove_sched(struct task_struct *task);

struct task_struct* find_next_task();
struct task_struct* find_in_sched(pid_t pid);

void move_to_expire(struct task_struct *task);
void reset_active();

unsigned int sched_find_first_bit();
struct task_struct* pcb_fetch(unsigned int priority);

void print_sched();
int loop(unsigned int argc, void *args);

int vmprog(unsigned int argc, void *args);
int kernel_execvm(unsigned int argc, void *args);

#endif
