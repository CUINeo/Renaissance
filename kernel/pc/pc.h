#ifndef _PC_H
#define _PC_H

#include <zjunix/list.h>
#include <zjunix/pid.h>
#include <zjunix/vm.h>
#include <zjunix/fs/fat.h>

//优先队列最大级数
#define MAX_PRIO 8
#define KERNEL_STACK_SIZE 4096
#define TASK_NAME_LEN 32

// 基本时间片为10ms 进程获得的时间片都是基本时间片的整数倍
#define BASIC_TIME_QUANTUM 10

// 进程类型 分为前台进程和后台进程
#define TYPE_FRONT 0
#define TYPE_BACK 1

// 进程状态 并用链表为每个进程维护一个状态队列
#define STATE_READY 0
#define STATE_RUNNING 1
#define STATE_WAITING 2
#define STATE_EXITED 3

// 在其他的文件中可能用到running_task这一全局变量
extern struct task_struct *running_task;


// regs_context: 寄存器信息结构，主要用于进程调度时的进程切换
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

// task_struct: 进程控制块PCB结构

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


// task_union联合体结构，同时存储PCB和进程的内核栈
union task_union{
    struct task_struct task;        //进程控制块
    unsigned char kernel_stack[KERNEL_STACK_SIZE];  //进程的内核栈
};
typedef struct regs_context context; // 上下文结构context




// 进程管理模块初始化 在init.c中调用用于初始化进程模块
void init_pc(); 
void init_lists(); // 进程链表初始化

// pc_create: 创建进程, entry为进程入口函数
int pc_create(char *task_name, void(*entry)(unsigned int argc, void *args), 
                int static_priority, unsigned int argc, void *args);

// pc_kill: 根据pid kill进程, 在shell中直接调用
int pc_kill(pid_t pid);

// loop: 一个while(1)的进程入口函数，用于Kill命令测试
int loop(unsigned int argc, void *args);

// pc_exit: 进程退出函数, 退出后根据调度算法选择下一个执行的进程
void pc_exit();
// for context switch in pc_exit defined by chenyuan
extern void switch_ex(struct regs_context* regs);

// pc_schedule: 进程调度函数, 在每次时钟中断时被调用, pt_context为当前的上下文信息
void pc_schedule(unsigned int status, unsigned int cause, context* pt_context);

// print_proc: 输出所有进程信息, 在shell命令为ps时直接调用
void print_proc();
// print_pcb: 输出对应pcb块管理的进程信息
void print_pcb(struct task_struct *task);

// print_xxx: ps的可选options -ready -running -waiting -exited
// 输出对应状态的进程信息
void print_ready();
void print_running();
void print_waiting();
void print_exited();

// prog: 测试进程的入口函数
int prog(unsigned int argc, void *args);
// kernel_exec: 由shell调用创建前台的内核态进程，默认入口函数为prog
int kernel_exec(unsigned int argc, void *args);

// get_curr_pcb: 其他模块中获取running_task的接口
struct task_struct* get_curr_pcb();

// pc_kill_syscall: 注册的syscall 在异常时强行结束当前进程
void pc_kill_syscall(unsigned int status, unsigned int cause, context* pt_context);

void task_files_delete(struct task_struct* task);

// add_xxx_queue: 在状态队列中加入进程
void add_tasks_queue(struct task_struct *task);
void add_ready_queue(struct task_struct *task);
void add_wait_queue(struct task_struct *task);
void add_exit_queue(struct task_struct *task);

// remove_xxx_queue: 从状态队列中移除进程
void remove_tasks_queue(struct task_struct *task);
void remove_ready_queue(struct task_struct *task);
void remove_wait_queue(struct task_struct *task);
void remove_exit_queue(struct task_struct *task);

// copy_context: defined in zjunix, 完成context从src到dest的复制
static void copy_context(context* src, context* dest);

// find_in_tasks: 根据pid在进程队列中查找其进程控制块
struct task_struct* find_in_tasks(pid_t pid);



// ============== O(1)scheduler functions 有关调度器的函数 ================= //

// add_sched: 
void add_sched(struct task_struct *task, int priority);
// remove_sched: 
void remove_sched(struct task_struct *task);

// move_to_expire: 在一个进程的时间片用完时调用，将其从活跃进程remove到过期队列，重置其时间片
void move_to_expire(struct task_struct *task);

// reset_active: 重置活跃进程队列，在活跃进程队列为空时将过期队列中的进程
void reset_active();

// find_next_task: 在pc_schedule中调用，在一个进程时间片用完或有进程退出时在调度队列中确认下一个运行进程
struct task_struct* find_next_task();

// find_in_sched: 根据pid在进程调度队列中查找对应的控制块
struct task_struct* find_in_sched(pid_t pid);

// O(1) scheduler以bitmap确认当前活跃进程, shced_find_first_bit返回当前活跃的最高位
unsigned int sched_find_first_bit();

// pcb_fetch: 根据优先权获取到对应的活跃队列的第一个pcb
struct task_struct* pcb_fetch(unsigned int priority);

// print_sched: 对应shell的sched命令调用，输出调度队列的信息
void print_sched();

// add_sleep_time: 在每次时钟中断时增加进程的avg_sleep
void add_sleep_time();

// pc_create_back: 创建一个后台进程, 优先级默认为4
int pc_create_back(char *task_name, void(*entry)(unsigned int argc, void *args), 
                    int static_priority, unsigned int argc, void *args);

// kernel_exec_back: shell中对应命令为 prog& task_name，创建内核态的后台进程
int kernel_exec_back(unsigned int argc, void *args);

// vmprog/kernel_execvm: 系统调用以及入口函数以测试虚拟内存, 但未能实现用户态
int vmprog(unsigned int argc, void *args);
int kernel_execvm(unsigned int argc, void *args);

// kernel_bg: 将前台进程转移到后台运行 shell调用为bg <pid>
int kernel_bg(pid_t pid);

// kernel_fg: 将后台进程转移到前台运行 shell调用为fg <pid>
int kernel_fg(pid_t pid);

#endif

