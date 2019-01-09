
/* * * * * * * * * * * * * * * * * * * *\
 * * * * * * * * * * * * * * * * * * * * 
 * * * * Process Controller Module * * *
 * * * * Written by YUAN Lin@ZJU * * * *
 * * * * * * * * * * * * * * * * * * * * 
 * * * * * * * * * * * * * * * * * * * *
 */

#include <zjunix/pc.h>
#include <arch.h>
#include <intr.h>
#include <zjunix/syscall.h>
#include <zjunix/utils.h>
#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <driver/ps2.h>
#include <zjunix/vm.h>

// 全局变量与全局进程队列

//就绪进程链表 ready:创建但还未被调度
struct list_head ready_list;
//进程等待链表 waiting: 还在执行中且等待被调度
struct list_head waiting_list;
//结束进程链表 exited: 执行完毕且已经退出
struct list_head exit_list;      
//所有进程链表 记录了所有的task, ps时需遍历输出
struct list_head tasks_list;       

// 在MIPS编译链架构下，用0替代null，否则会编译错误
struct task_struct *running_task = 0;   //当前运行进程


// Linux 2.6 O(1)调度器 调度队列
// 活动运行队列
struct list_head active[MAX_PRIO];

// 过期进程队列
struct list_head expire[MAX_PRIO]; 

// 每个优先级的基本时间片，由进程的优先级从高到低降序分配
// 时间片分配为基本时间片的整数倍，定义于pc.h BASIC_TIME_QUANTUM
unsigned int basic_time_slice[MAX_PRIO]; 

// 记录当前活动进程队列状态
// 当active[priority]中有正在运行或等待调度的进程, 对应的bitmap位置1
unsigned int sched_bitmap[MAX_PRIO]; 


/* 
 * copy_context:上下文切换时复制src的寄存器信息到dest处
 * @params src: 源上下文 dest: 目的上下文
 */
static void copy_context(context* src, context* dest) {
    dest->epc = src->epc;
    dest->at = src->at;
    dest->v0 = src->v0;
    dest->v1 = src->v1;
    dest->a0 = src->a0;
    dest->a1 = src->a1;
    dest->a2 = src->a2;
    dest->a3 = src->a3;
    dest->t0 = src->t0;
    dest->t1 = src->t1;
    dest->t2 = src->t2;
    dest->t3 = src->t3;
    dest->t4 = src->t4;
    dest->t5 = src->t5;
    dest->t6 = src->t6;
    dest->t7 = src->t7;
    dest->s0 = src->s0;
    dest->s1 = src->s1;
    dest->s2 = src->s2;
    dest->s3 = src->s3;
    dest->s4 = src->s4;
    dest->s5 = src->s5;
    dest->s6 = src->s6;
    dest->s7 = src->s7;
    dest->t8 = src->t8;
    dest->t9 = src->t9;
    dest->hi = src->hi;
    dest->lo = src->lo;
    dest->gp = src->gp;
    dest->sp = src->sp;
    dest->fp = src->fp;
    dest->ra = src->ra;
}



/* init_pc:进程控制模块初始化，在init_kernel()中被调用
 * @func: 创建1号进程idle并注册系统调用与中断
 * @func: 设置$9与$11以触发中断
 */
void init_pc() {

    init_lists(); // 进程链表初始化

    // 0号进程已经被pid_alloc给了init(虽然这个进程实质上未创建)

    // 初始化1号进程IDLE
    struct task_struct *idle;
    pid_t pid_idle;

    //创建idle进程，idle进程的task_struct结构位于内核代码部分(0-16MB)的最后一个页
    idle = (struct task_struct*)(kernel_sp - KERNEL_STACK_SIZE);
    pid_alloc(&pid_idle); // allocate pid:0
    kernel_strcpy(idle->name, "idle");

    idle->type = TYPE_BACK; // idle
    idle->pid = pid_idle; // pid = 1( pid = 0 的)
    idle->parent = IDLE_PID;
    idle->ASID = idle->pid;
    idle->state = STATE_READY;
    idle->avg_sleep = 0;

    //当前寄存器中的内容也就是idle进程的context内容
    idle->mm = 0;
    idle->task_files = 0;
    // idle是进程优先级最低的后台进程
    idle->static_priority = 7; 
    idle->counter = 10;

    // 初始化IDLE进程所在队列
    INIT_LIST_HEAD(&(idle->sched_node));
    INIT_LIST_HEAD(&(idle->state_node));
    INIT_LIST_HEAD(&(idle->tasks_node));
    add_tasks_queue(idle);
    add_ready_queue(idle);
    
    // 将IDLE进程加入调度链表
    add_sched(idle, 7);

    running_task = idle;

    // register_syscall(10, pc_kill_syscall);

    // 时钟中断时触发pc_schedule函数进行进程调度
    register_interrupt_handler(7, pc_schedule);

    // 设置cp0中的 compare/count 以触发中断
    asm volatile(
        "li $v0, 1000000\n\t"
        "mtc0 $v0, $11\n\t"
        "mtc0 $zero, $9");

    return;
}



/* init_lists:
 * @func: 进程链表与调度队列初始化
 * 设置基本时间片basic_time_slice
 */
void init_lists(){
    // INIT_LIST_HEAD: 初始化list，定义于zjunix/list.h
    INIT_LIST_HEAD(&ready_list);
    INIT_LIST_HEAD(&waiting_list);
    INIT_LIST_HEAD(&exit_list);
    INIT_LIST_HEAD(&tasks_list);

    // 调度链表初始化
    for(int i = 0; i < MAX_PRIO; i++){
        INIT_LIST_HEAD(&active[i]);
        INIT_LIST_HEAD(&expire[i]);
        // active队列bitmap初始化
        sched_bitmap[i] = 0;
    }

    // 基本时间片初始化
    // time_slice: 高优先级时间片更长
    // basic_time_quantum = (MAX_PRIO-priority) * BASIC_TIME_QUANTUM
    // e.g: pri0:cnt=1600 pri7:cnt=200(注意0是最高优先级)

    for(int i = 0; i < MAX_PRIO; i++){
        basic_time_slice[i] = BASIC_TIME_QUANTUM * (MAX_PRIO-i); 
    }

    return ;
}



/* pc_create: create a process
 * @func:创建进程，返回值代表是否创建成功
 * @para: task_name -- 进程名
 * @para: entry -- 进程入口函数
 * @para: argc -- 创建进程所用参数个数 args -- 创建进程所用参数
 * @ret: 1--success; 0--fail
 */

int pc_create(char *task_name, void(*entry)(unsigned int argc, void *args), 
                int static_priority, unsigned int argc, void *args){

    // 分配新进程的pid
    pid_t pid;
    if(pid_alloc(&pid) != 0){
        // 输出错误信息
        kernel_printf("pc_create: pid_allocate fail!\n");
        return 0;
    }else{
        // debug信息
        kernel_printf("pc_create: process %s <- pid %d allocated\n", task_name, pid);
    }
    

    // 创建进程的task_union
    union task_union *pcb_union;
    pcb_union = (union task_union*) kmalloc(sizeof(union task_union));
    if (pcb_union == 0) {
        // 输出错误信息
        kernel_printf("do fork: task union allocated failed!\n");
        return 0;
    }else{
        // debug信息
        // kernel_printf("pc_create: task_union created\n");
    }


    struct task_struct *pproc = running_task; //当前进程为新创建进程的父进程
    unsigned int init_gp; // heap起始位置

    // 对task_union维护的PCB信息进行初始化
    kernel_strcpy(pcb_union->task.name, task_name); // 进程名
    pcb_union->task.type = TYPE_FRONT; // 先默认是前台进程
    pcb_union->task.pid = pid; // pid
    pcb_union->task.parent = pproc->pid; // parent pid
    pcb_union->task.state = STATE_READY; // state
    
    pcb_union->task.avg_sleep = 0; // avg_sleep penalty on priority

    // 静态优先级通过参数设置
    pcb_union->task.static_priority = static_priority;
    pcb_union->task.counter = basic_time_slice[pcb_union->task.static_priority];

    INIT_LIST_HEAD(&(pcb_union->task.sched_node)); // 调度链表
    INIT_LIST_HEAD(&(pcb_union->task.state_node)); // 状态链表
    INIT_LIST_HEAD(&(pcb_union->task.tasks_node)); // 所有进程链表

    // regs_context初始化
    kernel_memset(&(pcb_union->task.context), 0, sizeof(struct regs_context));
    
    pcb_union->task.context.epc = (unsigned int)entry; //设置新进程入口地址
    pcb_union->task.context.sp = (unsigned int)pcb_union + KERNEL_STACK_SIZE; //设置新进程内核栈
    asm volatile("la %0, _gp\n\t" : "=r"(init_gp)); 
    pcb_union->task.context.gp = init_gp; 
    pcb_union->task.context.a0 = argc; // a0存储参数个数
    pcb_union->task.context.a1 = (unsigned int)args; // a1存储参数

    // 将进程控制块加入状态链表和调度链表
    add_tasks_queue(&(pcb_union->task)); // 加入所有进程队列
    add_ready_queue(&(pcb_union->task)); // 加入就绪状态队列(等待被调度)

    add_sched(&(pcb_union->task), 0);
    kernel_printf("Current task is %s\n", running_task->name);

    return 1;
}



/* kernel_exec
 * @func:从shell调用exec命令创建进程，调用于kernel/usr/ps.c(用于创建进程功能的物理测试)
 * @para:argc: 参数个数; args: 参数列表
 * 现在默认创建内核进程，入口为prog函数
 */
int kernel_exec(unsigned int argc, void *args){
    int create_flag = 0;
    kernel_printf("Enters function kernel_exec.\n");

    // 先不管user_mode和waitpid了
    // kernel_exec's static_priority use 0 as default
    create_flag = pc_create(args, (void*)prog, 0, argc, args);
    if(!create_flag){
        kernel_printf("kernel_exec: task_create failed!");
        return 0;
    }else{
        kernel_printf("kernel_exec: task_create success!");
    }

    return 1;
}



/* pc_exit
 * @func: 退出当前running的进程，按调度算法选择下一个进程并完成context switch
 */
void pc_exit(){

    // 无法退出IDLE/INIT进程
    if(running_task->pid == IDLE_PID){
        kernel_printf("pc_exit error: IDLE can not exit.");
    }else if(running_task->pid == INIT_PID){
        kernel_printf("pc_exit error: INIT can not exit.");
    }


    // 释放进程持有的文件控制块和内存控制块
    if(running_task->task_files != 0){
        task_files_delete(running_task);
    }

    // if(running_task->mm != 0){
        // mm_delete(running_task->mm);
    //}

    // 进入异常模式 => 中断关闭
    asm volatile (
        "mfc0  $t0, $12\n\t"
        "ori   $t0, $t0, 0x02\n\t"
        "mtc0  $t0, $12\n\t"
        "nop\n\t"
        "nop\n\t"
    );

    // 退出进程并进行调度
    struct task_struct *next_task;
    running_task->state = STATE_EXITED;

    remove_sched(running_task);
    add_exit_queue(running_task);

    int first_bit = -1;

    first_bit = sched_find_first_bit();

    if(first_bit == -1){
        // active队列中已经不存在任何活跃进程
        // 将expire队列中的待调度进程重新加入active队列
        reset_active();

        // reset后重新找到最高优先级的的active队列
        first_bit = sched_find_first_bit();
        next_task = pcb_fetch(first_bit);
    }else{
        // 根据first_bit直接获取该队列的第一个进程(O(1)所在)
        next_task = pcb_fetch(first_bit);
    }

    running_task = next_task;
    
    // vm
    // if(next->mm != 0){
    //     activate_mm(next);
    // }

// context switch
    switch_ex(&(running_task->context));


// never enters into the while(1)
    while(1){
        // error
    }
}


/* prog()
 * func: pc_create()参数中的entry入口函数，进程pretend运行函数
 */
int prog(unsigned int argc, void *args){
    
    // 输出提示信息，表明进入进程
    kernel_printf("Task Running: PID[%d]\n", running_task->pid);

    int cnt = 0;
    int loop = 0;
    while(1){
        cnt ++;
        if(cnt == 5000000){
            kernel_printf("......\n");
            loop++;
            cnt = 0;
        }
        if(loop == 5){
            kernel_printf("Exiting...\n");
            break;
        }
    }
    // 结束提示信息
    pc_exit();
    kernel_printf("Task End: PID[%d]\n", running_task->pid);
    
    return 1;
}


/* pc_kill
 * @func: 根据参数pid以kill进程，将其控制块从所在调度队列移除
 * @para: 进程号pid
 * @ret: 返回0代表kill失败，返回1代表kill成功
 */ 
int pc_kill(pid_t pid){
    struct task_struct *tasks_node, *state_node, *sched_node;

    // 0号进程IDLE和1号进程INIT不能kill，返回错误信息
    // 如果pid恰为当前进程，返回错误信息
    if(pid == 0){
        kernel_printf("pc_kill fail: PID[0] INIT cannot be killed.\n");
        return 0;
    }else if(pid == 1){
        kernel_printf("pc_kill fail: PID[1] IDLE cannot be killed.\n");
        return 0;
    }else if(pid == running_task->pid){
        kernel_printf("pc_kill fail: PID[%d] is the running process.\n", pid);
        return 0;
    }
    // 检查pid是否被分配
    if(pid_check(pid) == 0){ // pid_check(pid_t pid) defined in zjunix/pid.h
        kernel_printf("pc_kill fail: PID[%d] process not found.\n", pid);
        return 0;
    }

    // 在kill进程时要关闭中断
    disable_interrupts();

    // 删除状态链表/调度队列中的控制块
    tasks_node = find_in_tasks(pid); // 在tasks_queue中找到task_struct
    
    switch(tasks_node->state){
        case STATE_READY:
            remove_ready_queue(tasks_node);
            kernel_printf("pc_kill: PID[%d] has been removed from READY_QUEUE\n.", pid);
            break;
        case STATE_RUNNING:
            return 0;
            break;
        case STATE_WAITING:
            remove_wait_queue(tasks_node);
            kernel_printf("pc_kill: PID[%d] has been removed from WAITING_QUEUE\n.", pid);
            break;
        case STATE_EXITED: // 已经退出的进程无法被kill
            kernel_printf("pc_kill error: PID[%d] cannot removed from EXITED_QUEUE\n.", pid);
            break;
        default:
            // unrecognized state
            kernel_printf("pc_kill: PID[%d] has been removed from EXITED_QUEUE\n.", pid);
            return 0;
            break;
    }
    remove_sched(tasks_node);

    // 将进程控制块加入EXITED队列
    tasks_node->state = STATE_EXITED;
    add_exit_queue(tasks_node);

    // 将其文件控制块、内存控制块和pid释放
    if(pid_free(pid) != 0){ // pid可以再次在新进程中被allocate
        kernel_printf("pc_kill error: PID[%d] free fail!\n", pid);
    }else{
        kernel_printf("pc_kill: PID[%d] free.\n", pid);
    }

    if(tasks_node->task_files){
        kernel_printf("pc kill: PID[%d] task_files deleted.\n", pid);
        task_files_delete(tasks_node);
    }

    if(tasks_node->mm){
        kernel_printf("pc kill: PID[%d] memory deleted.\n", pid);
        // mm_delete(tasks_node->mm);
    }

    // TODO: 解决kill的进程的子进程处理


    // kill处理程序结束，恢复中断
    enable_interrupts();
    return 1;
}


/* pc_schedule
 * @func: 进程调度函数, 在每次时钟中断时调用
 * @para: status--CPU当前状态
 * @para: cause -- 当前中断的cause
 * @para: context* pt_context -- 当前进程的上下文信息
 */
void pc_schedule(unsigned int status, unsigned int cause, context* pt_context){
    // 保存context
    copy_context(pt_context, &(running_task->context));
    
    // 调用find_next_task函数，根据调度算法选取下一个进程
    struct task_struct *next_task;
    next_task = find_next_task();
    
    // 判断是否实际发生了process switch
    if(next_task != running_task){
        // kernel_printf("pc_schedule: next task %d pid\n", next_task->pid); // debug
        // find_next_task不改变running_task
        running_task = next_task;
        
    }

    // 加avg_sleep时间
    add_sleep_time();

    // 加载context,中断退出时把当前进程context加载到硬件寄存器中
    copy_context(&(running_task->context), pt_context);
    
    // mtc0将cp0中count寄存器reset，结束时钟中断
    asm volatile("mtc0 $zero, $9\n\t");

    return ;
}

/* find_next_task***
 * @func: 由O(1) scheduler的调度算法选择下一个进程
 * 重要函数，体现了采用的调度算法
 */
struct task_struct* find_next_task(){
    
    struct task_struct *next_task;
    unsigned int first_bit;

    //每次时钟中断时手动让时间片递减
    running_task->counter--;

    // 判断是否需要调度到其他进程
    if(running_task->counter == 0) {
        // debug 输出提示信息
        // kernel_printf("find_next_task: pid %d counter ends.\n", running_task->pid);

        // 根据O(1) scheduler的调度算法，将running_task移至过期队列
        move_to_expire(running_task);
        
        // TODO: 根据动态优先级调整调度队列

        first_bit = sched_find_first_bit();

        if(first_bit == -1){
            // active队列中已经不存在任何活跃进程
            // 将expire队列中的待调度进程重新加入active队列
            reset_active();

            // reset后重新找到最高优先级的的active队列
            first_bit = sched_find_first_bit();
            next_task = pcb_fetch(first_bit);
        }else{
            // 根据first_bit直接获取该队列的第一个进程(O(1)所在)
            next_task = pcb_fetch(first_bit);
        }

        return next_task;
    }else{ //当前进程时间片未用完，不改变现有进程
        return running_task;
    }

}


// ================================ utils functions ============================== //


/* get_curr_pcb called in mips32/exc.o
 * 满足其他模块调用，以获取当前进程PCB
 */
struct task_struct* get_curr_pcb() {
    return running_task;
}


/* pc_kill_syscall called in mips32/exc.o
 * @func: 满足系统调用，在init_pc中注册pc_kill_syscall
 */
void pc_kill_syscall(unsigned int status, unsigned int cause, context* pt_context) {
    
}



void task_files_delete(struct task_struct* task) {
    fs_close(task->task_files);
    kfree(&(task->task_files));
}


// add_tasks_queue
// 在总进程队列中加入进程
void add_tasks_queue(struct task_struct *task){
    // list_add_tail()定义于zjunix/list.h
    list_add_tail(&(task->tasks_node), &tasks_list);
}


// add_ready_queue
// 在就绪队列中加入进程
void add_ready_queue(struct task_struct *task){
    list_add_tail(&(task->state_node), &ready_list);
}


// add_wait_queue
// 在等待队列中加入进程
void add_wait_queue(struct task_struct *task){
    list_add_tail(&(task->state_node), &waiting_list);
}


// add_exit_queue
// 在退出队列中加入进程
void add_exit_queue(struct task_struct *task){
    list_add_tail(&(task->state_node), &exit_list);
}


// remove_tasks_queue
// 从总进程队列中删除进程
void remove_tasks_queue(struct task_struct *task){
    // list_del()定义于zjunix/list.h
    list_del(&(task->tasks_node));
    INIT_LIST_HEAD(&(task->tasks_node));
}


// remove_ready_queue
// 从就绪队列中删除进程
void remove_ready_queue(struct task_struct *task){
    list_del(&(task->state_node));
    INIT_LIST_HEAD(&(task->state_node));
}


// remove_wait_queue
// 从等待队列中删除进程
void remove_wait_queue(struct task_struct *task){
    list_del(&(task->state_node));
    INIT_LIST_HEAD(&(task->state_node));
}


// remove_exit_queue
// 从结束队列中删除进程
void remove_exit_queue(struct task_struct *task){
    list_del(&(task->state_node));
    INIT_LIST_HEAD(&(task->state_node));
}

/* find_in_tasks(pid_t pid)
 * @func: 在总进程队列中查找进程(search by pid)
 */
struct task_struct* find_in_tasks(pid_t pid){
    struct task_struct *next;
    struct list_head *p;

    list_for_each(p, &tasks_list) {
        next = container_of(p, struct task_struct, tasks_node);
        if (next->pid == pid)
            return next;
    }

    return 0;
}



// ======================== 对active/expire调度队列的操作 =======================//

/* add_sched
 * @func: 将进程加入active队列
 */
void add_sched(struct task_struct *task, int priority){
    
    task->state = STATE_WAITING;

    if(priority < 0 || priority > MAX_PRIO){
        kernel_printf("PC Error: add_sched() has used an invalid priority: %d\n", priority);
    }
    
    // 将进程加入active队列并置位bitmap
    else{
        // 这里调用list_move_tail而非list_add_tail
        // list_move_tail可以在移出原链表后保持双向链表的结构
        list_move_tail(&(task->sched_node), &active[priority]);
        sched_bitmap[priority] = 1;
    }
}



/* remove_sched(Not finished)
 * @func: 在进程退出或被kill时调用，从调度队列中移除对应的进程控制块
 * 不管指向的task此时是在active或是expire都要从队列中移除
 * 如果从active中remove后对应优先级的调度队列没有了进程
 * 需要将对应位的bitmap置0
 */
void remove_sched(struct task_struct *task){
    list_del(&(task->sched_node));
    INIT_LIST_HEAD(&(task->sched_node));
    
    // 如果task在活跃进程调度队列中
    // 将其对应的bitmap reset
    
    // 遍历reset bitmap
    for(int i = 0; i < MAX_PRIO; i++){
        if(list_empty(&active[i])){
            sched_bitmap[i] = 0;
        }
    }

    return ;
}



/* move_to_expire
 * @func: 在进程时间片用完后调用，将对应进程从active队列移至expire队列
 */
void move_to_expire(struct task_struct *task){
    
    unsigned int priority = -1;
    struct list_head *pos;
    // TODO: 这里不确定是不是要按照动态优先级计算
    priority = task->static_priority; //获取当前进程的静态优先级
    
    // 将task的pcb移动至相应的expire队列
    list_move_tail(&(task->sched_node), &expire[priority]);

    // 视remove后active队列的情况reset bitmap
    struct list_head *p;
    for(int i = 0; i < MAX_PRIO; i++){
        if(list_empty(&active[i])){
            // debug! 这里在一个队列多个进程时候reset会有bug
            // kernel_printf("reset bitmap\n");
            sched_bitmap[i] = 0; //remove时候可能有bit被reset为0
        }
    }
    return;
}



/* reset_active
 * @func: 在所有进程都被移动至过期队列后重置active
          将所有expire队列中的进程加回active并重置其时间片
 */
void reset_active(){
    
    struct list_head *pos;
    struct task_struct *pcb;
    // 遍历expire队列中的所有进程链表

    for(int i = 0; i < MAX_PRIO; i++){
        list_for_each(pos, &expire[i]){
            if(list_empty(&expire[i])){
                break;
            }
            if(pos->next == LIST_POISON2){
                break;
            }
            pcb = container_of(pos, struct task_struct, sched_node);
            
            if(pcb->pid <= 0)
                break;

            // reset counter 
            pcb->counter = basic_time_slice[i];

            // reset dynamic_priority
            // (according to the process type FRONT/BACK)
            if(pcb->avg_sleep >= 250){
                if(pcb->type == TYPE_FRONT){
                    if(pcb->static_priority >= 3){
                        pcb->avg_sleep = 0;
                    }else{
                        pcb->static_priority++;
                        pcb->avg_sleep = 0;
                    }
                }
                else if(pcb->type == TYPE_BACK){
                    if(pcb->static_priority >= 7){
                        pcb->avg_sleep = 0;
                    }else{
                        pcb->static_priority++;
                        pcb->avg_sleep = 0;
                    }
                }
            }

            // add_sched函数接收pcb和priority为参数，并将对应bitmap置1
            add_sched(pcb, pcb->static_priority);
        }
    }

    return;
}



/* find_in_sched
 * @func: 用pid在调度链表中查找进程，返回其控制块
 */
struct task_struct* find_in_sched(pid_t pid){
    struct task_struct *next;
    struct list_head *p;
    
    for(int i = 0; i < MAX_PRIO; i++){
        list_for_each(p, &active[i]){
            next = container_of(p, struct task_struct, sched_node);
            if( next->pid == pid )
                return next;
        }
        list_for_each(p, &expire[i]){
            next = container_of(p, struct task_struct, sched_node);
            if( next->pid == pid )
                return next;
        }
    }
    
    return 0; // not found the process in sched
}



/* sched_find_first_bit
 * @func: 在sched_bitmap中选取当前优先级最高的队列
 * 如果返回-1，代表active列表为空，需重置active&expire
 */
unsigned int sched_find_first_bit(){
    unsigned int next_priority = -1;
    
    // 当bitmap全部为0时，返回-1，这时需要重置expire和active了
    for(int i = 0; i < MAX_PRIO; i++){
        if(sched_bitmap[i] == 1){
            next_priority = i;
            break;
        }
    }
    return next_priority;
}




/* pcb_fetch
 * @func: 配合sched_find_first_bit使用
 *        在找到first_bit后，获取对应的task_struct结构
 */
struct task_struct* pcb_fetch(unsigned int priority){
    struct task_struct *pcb;
    struct list_head *pos;

    // 如果在pcb_fetch时相应bitmap位为0，显然出现了问题
    if(sched_bitmap[priority] != 1){
        kernel_printf("Scheduling Error: pcb_fetch tries to fetch unexisted priority.\n");
        return 0;
    }

    // 通过container_of函数由sched_node找到其task_struct结构地址
    pos = &(active[priority]);
    list_for_each(pos, &(active[priority])){
        pcb = container_of(pos, struct task_struct, sched_node);
    }

    return pcb;
}



/* print_pcb(task_struct* task)
 * @func: 输出进程控制块task对应的metadata
 */
void print_pcb(struct task_struct *task) {
    
    // 暂定为输出这些信息，需要debug或者最后修改的时候随时可变

    kernel_printf("%s\t %d\t\t %d\t\t",task->name, task->pid, task->parent);

    if(task->state == STATE_READY)
        kernel_printf("READY\n");
    else if(task->state == STATE_RUNNING)
        kernel_printf("RUNNING\n");
    else if(task->state == STATE_WAITING)
        kernel_printf("WAITING\n");
    else if(task->state == STATE_EXITED)
        kernel_printf("EXITED\n");

    return;
}



/* print_proc
 * @func: 输出所有进程信息，对应shell中的ps命令
 */
void print_proc(){
    struct task_struct *next;
    struct list_head *p;

    running_task->state = STATE_RUNNING; // easy implementation

    kernel_printf("NAME\t PID\t PPID\t STATE\n");
    
    list_for_each(p, &tasks_list){
        next = container_of(p, struct task_struct, tasks_node);
        print_pcb(next);
    }
    return ;
}



/* print_sched
 * @func: 输出所有调度链表，在debug阶段十分重要
 */
void print_sched(){
    struct task_struct *next;
    struct list_head *p;
    kernel_printf("active: \n");
    for(int i = 0; i < MAX_PRIO; i++){
        kernel_printf("active[%d]: ", i);
        list_for_each(p, &active[i]){
            next = container_of(p, struct task_struct, sched_node);
            kernel_printf("PID %d\t", next->pid);
        }
        kernel_printf("\n");
    }

    kernel_printf("expire: \n");
    for(int i = 0; i < MAX_PRIO; i++){
        kernel_printf("expire[%d]: ", i);
        list_for_each(p, &expire[i]){
            next = container_of(p, struct task_struct, sched_node);
            kernel_printf("PID %d\t", next->pid);
        }
        kernel_printf("\n");
    }

    return ;
}




// =========== 为顶层调用提供接口进行其他功能的测试 ============= ///



/*
 * 需要加一个多进程调度的测试函数
 */
int test_sched(unsigned int argc, void *args){




    return 1;
}




/* loop进程入口函数
 * @func: 需要加一个测试pc_kill功能的死循环函数
 */
int loop(unsigned int argc, void *args){
    
    // 输出提示信息，表明进入进程
    kernel_printf("Loop task Running: PID[%d]\n", running_task->pid);

    kernel_printf("while(1){\n");
    kernel_printf("}\n");

    while(1){
        // loop进程执行一个死循环, 等待kill调用杀死进程
    }

    // 结束提示信息
    pc_exit();
    kernel_printf("Task End: PID[%d]\n", running_task->pid);
    return 1;
}


int kernel_execvm(unsigned int argc, void *args){
    int create_flag = 0;
    kernel_printf("Enters function kernel_execvm.\n");

    // 先不管user_mode和waitpid了
    create_flag = pc_create(args, (void*)vmprog, 0, argc, args);
    if(!create_flag){
        kernel_printf("kernel_execvm: task_create failed!");
        return 0;
    }else{
        kernel_printf("kernel_execvm: task_create success!");
    }

    return 1;
}

/* vmprog 入口函数
 */
// 给cf测试tlb_refill用
int vmprog(unsigned int argc, void *args) {
    int i;

    kernel_printf("after refill.\n");

    unsigned int * test_addr = (unsigned int*)0;
    unsigned int test_val = 0, test_val2;
    *test_addr = 3;
    for (i = 0; i < 20; i++) {
        //重复访问用户空间虚拟地址,触发TLB miss异常,进行TLB refill处理操作
        test_addr = (unsigned int*)(i * 4);
        test_val += 3;
        *test_addr = test_val;
        kernel_printf("%x Write content:%x      ", (unsigned int)test_addr, test_val);
        test_val2 = *test_addr;
        kernel_printf("Read content: %x\n", test_val2);
    }

    #ifdef TLB_DEBUG
    kernel_getchar();
    #endif

    kernel_printf("Test success!\n");
    kernel_getchar();
    
    //进程退出
    pc_exit(0);
    return 0;
}


/* add_sleep_time
 * @func: 在pc_schedule时为除了当前进程的其他进程增加avg_sleep
 */
void add_sleep_time(){
    struct task_struct *next;
    struct list_head *p;
    
    list_for_each(p, &tasks_list){
        next = container_of(p, struct task_struct, tasks_node);
        if(next != running_task){
            next->avg_sleep++;
        }   
    }
    return ;

}






