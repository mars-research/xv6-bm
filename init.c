// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };
static __inline__ unsigned long long rdtsc(void)
{
    unsigned long long int x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}
int
main(void)
{
  int pid, res;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid<0){
      printf(1,"init: cannot fork\n");
      for(;;);
    }
    else{
      if(pid == 0){
        printf(1,"init: dispatch child process\n");
        struct msg ret;    
        res = rdispatch(&ret);
        //unsigned long long retval = rdtsc() - *((unsigned long long *)&ret);
        printf(1, "message recieved from parent: %x from pid %d\n",(unsigned int)ret.regs[0], res);
        ret.regs[0] = 0x1badb002;
        rcall(res,0, &ret);
      }
      else{
        struct msg ret;
        ret.regs[0] = 0x1badb001;
        sleep(50);
        
        printf(1, "message sent to child: %x to %d\n", ret.regs[0], pid);
       // *((unsigned long long *)&ret) = rdtsc();
        res = rcall(pid,1, &ret);
        printf(1, "message parent recieved back from child: %x from pid %d\n",*ret.regs, res);
      }

      
    }
    for(;;);
  }
}
