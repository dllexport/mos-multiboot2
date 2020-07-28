#include "task.h"
#include <memory/flags.h>
#include "tss.h"
#include <thread/regs.h>
#include <std/printk.h>
#include <std/debug.h>
#include <memory/physical.h>
#include <std/interrupt.h>
#include <memory/mapping.h>
#include <memory/kmalloc.h>

// we pop all pt_regs out
// then restore the stack to rsp0(stack base)
// then call the fn
// then do_exit
extern "C" void kernel_thread_func();
// if not kernel thread, return to userspace
extern "C" void ret_syscall();

static task_struct *init_task;

extern "C" unsigned long do_exit(unsigned long code)
{
    printk("init2 finished\n");

    while (1)
        ;
}

static uint64_t do_fork(struct Regs *regs, unsigned long clone_flags)
{
    auto page = PhysicalMemory::GetInstance()->Allocate(1, PG_PTable_Maped | PG_Kernel | PG_Active);

    auto stack_start = (uint64_t)(Phy_To_Virt(page->physical_address));

    auto task = (task_struct *)Phy_To_Virt(page->physical_address);

    memset(task, 0, sizeof(*task));
    // *task = *current;

    list_init(&task->list);
    list_add_to_behind(&init_task->list, &task->list);

    task->pid++;
    task->state = TASK_UNINTERRUPTIBLE;

    // place thread_struct after task_struct
    auto thread = (struct thread_struct *)(task + 1);
    task->thread = thread;

    regs->rsp = (uint64_t)task + STACK_SIZE;
    regs->rbp = regs->rsp;

    // copy to regs to the stack end
    memcpy((void *)((uint8_t *)task + STACK_SIZE - sizeof(Regs)), regs, sizeof(Regs));

    // stack end is equal to stack base
    thread->rsp0 = regs->rsp;
    thread->rip = regs->rip;
    // the real stack points stack end - pt_regs
    thread->rsp = (uint64_t)task + STACK_SIZE - sizeof(Regs);

    if (!(clone_flags & PF_KTHREAD))
    {
        thread->rip = (uint64_t)Phy_To_Virt(&ret_syscall);
    }

    task->state = TASK_RUNNING;

    return 0;
}

static int create_kernel_thread(void (*fn)(), uint64_t arg, uint64_t flags)
{

    Regs regs;
    memset(&regs, 0, sizeof(regs));

    regs.rbx = (uint64_t)fn;

    regs.cs = KERNEL_CS;

    regs.rflags = (1 << 9); // interrupt enable
    regs.rip = (uint64_t)&kernel_thread_func;

    return do_fork(&regs, flags | PF_KTHREAD);
}

extern "C" void ret_syscall();

#include "userland.h"

// the kernel pml4
extern "C" char pml4;
extern "C" char pdpe;
extern "C" char pde;

int vmap_copy_kernel(task_struct *task) {
    auto kernel_pml4 = (Page_PML4*)Phy_To_Virt(&pml4);
    task->mm->pml4[511] = kernel_pml4[511];
}

void userland_page_init(task_struct *task)
{
    vmap_frame(current, 0x400000, 0x07);
    vmap_copy_kernel(current);
    task->mm->start_code = (void *)0x400000;
    task->mm->end_code = (void *)0x400000 + PAGE_4K_SIZE;
}

void init2()
{
    cli();
    printk("this is init 2\n");

    auto task = current;
    task->mm = (mm_struct *)((char *)task->thread + sizeof(thread_struct));
    bzero(task->mm, sizeof(mm_struct));
    userland_page_init(task);
    set_cr3(Virt_To_Phy(task->mm->pml4));
    memcpy((void *)task->mm->start_code, (uint8_t *)&userland_entry, 1024);
    auto ret_syscall_addr = uint64_t(&ret_syscall);
    auto ret_stack = uint64_t((uint8_t *)task + STACK_SIZE - sizeof(Regs));
    asm volatile("movq	%0,	%%rsp	\n\t"
                 "pushq	%1		    \n\t" ::
                     "m"(ret_stack),
                 "m"(ret_syscall_addr)
                 : "memory");

    auto regs = (Regs *)ret_stack;
    bzero(regs, sizeof(Regs));
    regs->rsp = (uint64_t)(0x402fff & 0xfffff0); // RSP
    regs->rbp = regs->rsp;                       // RSP
    regs->rcx = (uint64_t)(0x400000);            // RIP
    regs->r11 = (1 << 9);

    printk("enter userland\n");

    asm volatile("retq");
}

uint64_t init(uint64_t arg)
{
    printk("this is kernel thread\n");

    create_kernel_thread(&init2, 1, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
    auto next = (task_struct *)list_next(&current->list);
    printk("current rsp : %x\n", current->thread->rsp0);
    printk("next rsp : %x\n", next->thread->rsp0);
    // switch_to(current, next);
    asm volatile("sti");
    while (1)
    {
        // printk("1");
    }
}

void task_init()
{

    auto page = PhysicalMemory::GetInstance()->Allocate(1, PG_PTable_Maped | PG_Kernel | PG_Active);

    auto init_task_stack = (void *)(Phy_To_Virt(page->physical_address) + PAGE_4K_SIZE);

    tss_struct init_task_tss;
    bzero(&init_task_tss, sizeof(tss_struct));
    init_task_tss.rsp0 = (uint64_t)init_task_stack;

    set_tss(init_task_tss);

    init_task = (task_struct *)Phy_To_Virt(page->physical_address);

    memset(init_task, 0, STACK_SIZE);

    list_init(&init_task->list);

    init_task->state = TASK_UNINTERRUPTIBLE;
    init_task->flags = PF_KTHREAD;
    init_task->pid = 0;
    init_task->signal = 0;
    init_task->priority = 0;

    // set mm and thread

    init_task->mm = nullptr;

    auto thread = (struct thread_struct *)(init_task + 1);
    init_task->thread = thread;
    thread->fs = KERNEL_DS;
    thread->gs = KERNEL_DS;
    thread->rsp0 = (uint64_t)init_task + STACK_SIZE;
    thread->rsp = (uint64_t)init_task + STACK_SIZE - sizeof(Regs);
    thread->rip = uint64_t(&init);
    // the real stack points stack end - Regs
    init_task->state = TASK_RUNNING;

    asm volatile("movq	%0,	%%fs \n\t" ::"a"(init_task->thread->fs));
    asm volatile("movq	%0,	%%gs \n\t" ::"a"(init_task->thread->gs));
    asm volatile("movq	%0,	%%rsp \n\t" ::"a"(init_task->thread->rsp));
    asm volatile("movq	%0,	%%rbp \n\t" ::"a"(init_task->thread->rsp0));
    asm volatile("push  %0 \n\t" ::"a"(init_task->thread->rip));
    asm volatile("retq");
}

void schedule()
{
    auto next = (task_struct *)list_prev(&current->list);
    printk("from %d to %d\n", current->pid, next->pid);
    switch_to(current, next);
}

extern "C" void __switch_to(struct task_struct *prev, struct task_struct *next)
{

    auto &task_tss = get_tss();
    task_tss.rsp0 = next->thread->rsp0;
    set_tss(task_tss);

    asm volatile("movq	%%fs,	%0 \n\t"
                 : "=a"(prev->thread->fs));
    asm volatile("movq	%%gs,	%0 \n\t"
                 : "=a"(prev->thread->gs));

    asm volatile("movq	%0,	%%fs \n\t" ::"a"(next->thread->fs));
    asm volatile("movq	%0,	%%gs \n\t" ::"a"(next->thread->gs));

    if (prev->mm == nullptr && next->mm)
    {
        // printk("kernel to userland\n");
        set_cr3(Virt_To_Phy(next->mm->pml4));
        flush_tlb();
    }
    else if (prev->mm && next->mm == nullptr)
    {
        // printk("userland to kernel\n");
        set_cr3(&pml4);
        flush_tlb();
    }
    asm volatile("sti");
}
