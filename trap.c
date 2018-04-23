#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
uint nopreempt;

void dump_state(struct trapframe *tf);

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

void _dump_stack(unsigned int stack) {
  /* Assume that entire stack page is mapped */
  unsigned int roundup = PGROUNDUP(stack); 
  unsigned int counter = 0; 

  /* If we're exactly at the start of the page, 
     dump next page, well, really we don't know 
     whether the stack is empty or full, so the 
     next page might be unmapped 
   */
  if (roundup == stack)
    roundup = roundup + PGSIZE;

  cprintf("stack starting at:%x\n", stack); 

  /* Dump as words (4 bytes) in groups of 16, but dump the 
     last 1-4 bytes individually, in case we're spill out in the 
     next page that might be unmapped */
  cprintf("%x", stack); 
  while (stack < roundup - sizeof(void *)) {
    cprintf("%x ", *(unsigned int *)stack); 
    stack += sizeof(void *); 
    if (counter == 15) {
      counter = 0;
      cprintf("\n");
      cprintf("%x:", stack);
    }
    counter ++; 
  }

  cprintf(" ");

  /* If any bytes left 1-4 dump them as bytes */
  while (stack < roundup) {
    cprintf("%c ", *(char*)stack); 
    stack ++; 
  }

  cprintf("\n");

}

void dump_stack(char *s) {
  _dump_stack((unsigned int)&s);
}

void dump_stack_addr(unsigned int a) {
  _dump_stack(a);
}

void dump_state(struct trapframe *tf) {
  cprintf("eax: %x, ebx: %x, ecx: %x, edx: %x\n",
          tf->eax, tf->ebx, tf->ecx, tf->edx);
  cprintf("esp: %x, ebp: %x, esi: %x, edi: %x\n",
          tf->esp, tf->ebp, tf->esi, tf->edi);
  cprintf("gs: %x, fs: %x, es: %x, ds: %x, ss: %x\n",
          tf->gs, tf->fs, tf->es, tf->ds, tf->ss);
  cprintf("err: %x, eip: %x, cs: %x, esp: %x, eflags: %x\n",
          tf->err, tf->eip, tf->cs, tf->esp, tf->eflags);
}

void dump_kernel(struct trapframe *tf) {

  dump_state(tf); 

  if (mycpu()->proc)
    cprintf("current process, id: %d, name:%s\n", 
          mycpu()->proc->pid, mycpu()->proc->name);
  else 
    cprintf("current process is NULL\n"); 

  if (mycpu()->proc && mycpu()->proc->tf != tf)
    dump(); 

  /* Inside the trap function, tf is on top of the stack */
  _dump_stack((unsigned int)tf);
  return;
};

void dump() {
  if (!mycpu()->proc) {
     cprintf("current process is NULL\n");
     return;
  }

  cprintf("state of the current process, id: %d, name:%s\n", 
          mycpu()->proc->pid, mycpu()->proc->name);

  dump_state(mycpu()->proc->tf); 
  return;
};

//PAGEBREAK: 41
void trap(struct trapframe *tf)
{
  int num;

//if(PGROUNDUP((unsigned int)tf) - (unsigned int) tf > 500){
//    cprintf("stack page is full: tf:%x\n", tf);    
//  }
    
  if (tf->trapno == T_SYSCALL)
  {
    if (cpus[0].proc->killed)
    {
      exit();
    }
    cpus[0].proc->tf = tf;
    num = tf->eax;
    if (num > 0 && num < NELEM(syscalls) && syscalls[num])
    {
      tf->eax = syscalls[num]();
    }
    else
    {
      cprintf("%d %s: unknown sys call %d\n",
              cpus[0].proc->pid, cpus[0].proc->name, num);
      tf->eax = -1;
    }
    if (cpus[0].proc->killed)
    {
      exit();
    }

    return;
  }

  switch (tf->trapno)
  {
  case T_IRQ0 + IRQ_TIMER:
    if (cpuid() == 0)
    {
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE + 1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if (myproc() == 0 || (tf->cs & 3) == 0)
    {
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      dump_kernel(tf); 
      dump();
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    dump_state(tf);
    dump_stack_addr(0);
    dump_stack_addr(tf->esp); 
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if (!nopreempt && myproc() && myproc()->state == RUNNING &&
      tf->trapno == T_IRQ0 + IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();
}
