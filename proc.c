#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

int int_count; 
int switch_count; 
int empty_rvp; 

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
struct {
  struct{
    struct proc * p;
    struct msg m __attribute__ ((aligned (16)));
  } endpoints[NENDS];
} ipc_endpoints;
static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->sysenter_kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  p->sysenter_kstack += KSTACKSIZE;

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    kfree(np->sysenter_kstack - KSTACKSIZE);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        kfree(p->sysenter_kstack - KSTACKSIZE);
        p->sysenter_kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      switch_count ++; 

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1){
    panic("sched locks");
  }
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
  
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]        "unused      ",
  [EMBRYO]        "embryo      ",
  [SLEEPING]      "sleep       ",
  [RUNNABLE]      "runble      ",
  [RUNNING]       "run         ",
  [ZOMBIE]        "zombie      ",
  [IPC_DISPATCH]  "ipc dispatch"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
__attribute__((always_inline)) static inline int
_fetchint(uint addr, int *ip, struct proc * curproc)
{

  if(addr >= curproc->sz || addr+4 > curproc->sz)
    return -1;
  *ip = *(int*)(addr);
  return 0;
}

__attribute__((always_inline)) static inline int
_argint(int n, int *ip, struct proc * curproc) 
{
  return _fetchint((curproc->tf->esp) + 4 + 4*n, ip, curproc);
}

__attribute__((always_inline)) static inline void
copy_msg(struct msg * from, struct msg * to){
/*for some reason, trap on sse
  asm("movaps (%0),%%xmm0\n\t"
      "movntps %%xmm0,(%1) "::"r" (m1),"r"(m2));
  m1 = (struct msg *)(((char *)m1)+4);
  m2 = (struct msg *)(((char *)m2)+4);
  asm("movaps (%0),%%xmm0\n\t"
      "movntps %%xmm0,(%1) "::"r" (m1),"r"(m2));
  m1 = (struct msg *)(((char *)m1)+4);
  m2 = (struct msg *)(((char *)m2)+4);
  asm("movaps (%0),%%xmm0\n\t"
      "movntps %%xmm0,(%1) "::"r" (m1),"r"(m2));
  m1 = (struct msg *)(((char *)m1)+4);
  m2 = (struct msg *)(((char *)m2)+4);
  asm("movaps (%0),%%xmm0\n\t"
      "movntps %%xmm0,(%1) "::"r" (m1),"r"(m2));
      */
 // *to = *from;   
}
__attribute__((always_inline)) static inline int
_argptr(int n, char **pp, int size, struct proc * curproc)
{
  int i;
 
  if(_argint(n, &i, curproc) < 0)
    return -1;
  if(size < 0 || (uint)i >= curproc->sz || (uint)i+size > curproc->sz)
    return -1;
  *pp = (char*)i;
  return 0;
}

__attribute__((always_inline))
static inline void disable_preempt(){
  nopreempt = 1;
}
__attribute__((always_inline))
static inline void enable_preempt(){
  nopreempt = 0;
}

__attribute__((always_inline))
static inline void _pushcli(){
  //nopreempt = 1;
}
__attribute__((always_inline))
static inline void _popcli(){
  //nopreempt = 0;
}

#define ITERS 1000000

int
test_one_pgdir(void)
{
  unsigned long i; 
  unsigned long long start, end; 
  struct proc *p;
  struct cpu  *c;
  
  c = &cpus[0];
  p = c->proc;
        
  start = rdtsc();
  for(i = 0; i < ITERS - 1; i++){
    lcr3(V2P(p->pgdir));
  }
  end = rdtsc();
        
  cprintf("overhead of loading CR3 average cycles %d across runs: %d\n",
        ITERS, (unsigned long)(end - start)/ITERS);
  return 0;
}

int
test_two_pgdir(void)
{
  unsigned long i, found = 0; 
  unsigned long long start, end; 
  struct proc *p, *p2;
  struct cpu  *c;
  
  c = &cpus[0];
  p = c->proc;

  for(p2 = ptable.proc; p2 < &ptable.proc[NPROC]; p2++)
    if((p2->state != UNUSED && p2->state != EMBRYO) && p2->pid != p->pid) {
      found = 1;
      break;
    }

  if(!found) {
    cprintf("failed to find the second process\n");
    return -1;
  }  
        
  start = rdtsc();
  for(i = 0; i < ITERS - 1; i++){
    lcr3(V2P(p2->pgdir));
    lcr3(V2P(p->pgdir));
  }
  end = rdtsc();
        
  cprintf("overhead of switching one page table to another and back cycles %d across runs: %d\n",
        ITERS, (unsigned long)(end - start)/ITERS);
  return 0;
}

int
test_two_pgdir_two_pagewalks(void)
{
  unsigned long i, found = 0; 
  unsigned long long start, end; 
  struct proc *p, *p2;
  struct cpu  *c;
  int *pcode = 0x0; 
  int *pstack = (int*)0x2000;
  int sum = 0; 
  
  c = &cpus[0];
  p = c->proc;

  for(p2 = ptable.proc; p2 < &ptable.proc[NPROC]; p2++)
    if((p2->state != UNUSED && p2->state != EMBRYO) && p2->pid != p->pid) {
      found = 1;
      break;
    }

  if(!found) {
    cprintf("failed to find the second process\n");
    return -1;
  }  
        
  start = rdtsc();
  for(i = 0; i < ITERS - 1; i++){
    lcr3(V2P(p2->pgdir));
    sum += (*pcode) + (*pstack);  
    lcr3(V2P(p->pgdir));
    sum += (*pcode) + (*pstack);  
  }
  end = rdtsc();
        
  cprintf("overhead of switching one page table to another and back with 2 page walks: cycles %d across runs: %d, sum:%d\n",
        ITERS, (unsigned long)(end - start)/ITERS, sum);
  return 0;
}

int
sys_test_pgdir(void)
{
  acquire(&ptable.lock);

  test_one_pgdir();
  test_two_pgdir(); 
  test_two_pgdir_two_pagewalks(); 

  release(&ptable.lock);
  return 0; 
};
int
sys_int_null(void)
{
  return 0;
}

int
sys_sysenter_null(void)
{
  return 0;
}

int
sys_send_recv_dummy(void)
{
  int endp = 0;
  //struct msg * message;
  struct proc *p;
  struct proc *mine;
  struct cpu  *c;
  _pushcli();

  c = &cpus[0];
  mine = c->proc;
/*  if(__builtin_expect(_argint(0, &endp, mine) < 0||_argptr(1,(char**)&message,sizeof(struct msg), mine)<0, 0)){
    _popcli();
    cprintf("send_recv: wrong args\n");
    return -1;
  }
 */   
  //cprintf("send_recv: endp:%d\n", endp);

  p = ipc_endpoints.endpoints[endp].p;
  p = mine; 
  if (!p || (p->state != IPC_DISPATCH))
  {
    _popcli();
    empty_rvp ++; 
    //cprintf("shouldn't happen, p:%x, endp:%d\n", p, endp);
    //if(p) cprintf("state:%d\n", p->state);
    //return -2;
  }

  //copy_msg(message,&ipc_endpoints.endpoints[endp].m);
  
  p->state = RUNNING;
  mine->state = IPC_DISPATCH;
  ipc_endpoints.endpoints[endp].p = mine;
  c->proc = p;
  c->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  
  lcr3(V2P(p->pgdir));
  lcr3(V2P(mine->pgdir));

  //swtch(&mine->context, p->context);
  
  //_popcli();
  //copy_msg(&ipc_endpoints.endpoints[endp].m, message);
  return 1;
}

int
sys_send_recv(void)
{
  int endp = 0;
  //struct msg * message;
  struct proc *p;
  struct proc *mine;
  struct cpu  *c;
  _pushcli();

  c = &cpus[0];
  mine = c->proc;
/*  if(__builtin_expect(_argint(0, &endp, mine) < 0||_argptr(1,(char**)&message,sizeof(struct msg), mine)<0, 0)){
    _popcli();
    cprintf("send_recv: wrong args\n");
    return -1;
  }
 */   
  //cprintf("send_recv: endp:%d\n", endp);

  p = ipc_endpoints.endpoints[endp].p;
  if (!p || (p->state != IPC_DISPATCH))
  {
    _popcli();
    empty_rvp ++; 
    cprintf("shouldn't happen, p:%x, endp:%d\n", p, endp);
    if(p) cprintf("state:%d\n", p->state);
    return -2;
  }

  //copy_msg(message,&ipc_endpoints.endpoints[endp].m);
  
  p->state = RUNNING;
  mine->state = IPC_DISPATCH;
  ipc_endpoints.endpoints[endp].p = mine;
  c->proc = p;
  c->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  
  lcr3(V2P(p->pgdir));
  swtch(&mine->context, p->context);
  
  //_popcli();
  //copy_msg(&ipc_endpoints.endpoints[endp].m, message);
  return 1;
}
int
sys_send(void)
{
  int endp = 0;
  struct msg * message;
  struct proc *p;
  struct proc *mine;
  struct cpu  *c;
  int cnt, s_cnt, e_cnt; 
  _pushcli();
  c = &cpus[0];
  mine = c->proc;

  cnt = int_count;
  s_cnt = switch_count;
  e_cnt = empty_rvp;  
 
/* 
  if(_argint(0, &endp, mine) < 0||_argptr(1,(char**)&message,sizeof(struct msg), mine)<0){
    _popcli();
    return -1;
  }
*/
  //cprintf("send: endp:%d\n", endp); 
  p = ipc_endpoints.endpoints[endp].p;
  if (p!=0?p->state!=IPC_DISPATCH:1)
  {
    _popcli();
    return -2;
  }
  
  copy_msg(message,&ipc_endpoints.endpoints[endp].m);
  p->state = RUNNING;
  mine->state =RUNNABLE;
  ipc_endpoints.endpoints[endp].p = 0;
  c->proc = p;
  c->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  lcr3(V2P(p->pgdir));
  swtch(&mine->context, p->context);
  release(&ptable.lock);
  enable_preempt();

  cprintf("interrupt count:%d, sched count:%d, empty_rvp:%d\n", cnt, s_cnt, e_cnt);

  return 1;
}

int
sys_recv(void)
{
  //cprintf("HELLO FROM RECV\n");
  int endp = 0;
  struct msg * message = 0;
  //if(argint(0, &endp) < 0||argptr(1,(char**)&message,sizeof(struct msg))<0)
  //  return -1;
  return recv(endp, message);

}

int recv(int endp, struct msg *message)
{
  struct proc *p;
  
  int_count = 0; 
  switch_count = 0; 
  empty_rvp = 0;

/*
  //cprintf("recv: endp:%d\n", endp);
  if(ipc_endpoints.endpoints[endp].p!=0) {
    copy_msg(&ipc_endpoints.endpoints[endp].m, message);
    return -1;
  }
*/
  
  disable_preempt();
  p = myproc(); 
  ipc_endpoints.endpoints[endp].p = p;
  p->state = IPC_DISPATCH;
  acquire(&ptable.lock);
  swtch(&p->context, mycpu()->scheduler);
  _popcli();
  copy_msg(&ipc_endpoints.endpoints[endp].m, message);
  return 1;
}
