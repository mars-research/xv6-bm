// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#define uint32_t unsigned int
#define uint64_t unsigned long long
uint32_t UInt64DivAndGetMod(uint64_t *a, uint32_t b) {
  uint32_t upper = ((uint32_t*)a)[1], r;
  ((uint32_t*)a)[1] = 0;
  if (upper >= b) {   
    ((uint32_t*)a)[1] = upper / b;
    upper %= b;
  }
  __asm__("divl %2" : "=a" (((uint32_t*)a)[0]), "=d" (r) :
      "rm" (b), "0" (((uint32_t*)a)[0]), "1" (upper));
  return r;
}

char *argv[] = { "sh", 0 };
#define ITERS 1000000
static __inline__ unsigned long long rdtsc(void)
{
    unsigned long long int x;
    __asm__ volatile ("rdtsc" : "=A" (x));
    return x;
}
int
main(void)
{
  int pid;
  unsigned long long tv;
  unsigned long long sum;
  sum = 0;
  int num;
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
        struct msg ret __attribute__ ((aligned (16)));    
        recv(0,&ret);
        tv = rdtsc();
        //unsigned long long retval = rdtsc() - *((unsigned long long *)&ret);
        for(num = 0;num<ITERS-1;num++){
        *((unsigned long long*)ret.regs) =  rdtsc();
        
        send_recv(0, &ret);
        tv = rdtsc();
        sum +=(tv - *((unsigned long long *)ret.regs));
        }
        
        
        UInt64DivAndGetMod(&sum, ITERS-1);
        
        printf(1, "message recieved from parent: %d cycles taken\n",(uint)(sum));
        *((unsigned long long*)ret.regs) =  rdtsc();  
        send(0, &ret);
      }
      else{
        struct msg ret __attribute__ ((aligned (16)));
        ret.regs[0] = 0x1badb001;
        sleep(50);
        
        
        for(num = 0;num<ITERS;num++){
        *((unsigned long long*)&ret.regs) = rdtsc();
         send_recv(0, &ret);
        tv = rdtsc();
        sum +=(tv - *((unsigned long long *)ret.regs));
        }
        UInt64DivAndGetMod(&sum, ITERS);
        printf(1, "message parent recieved back from child: %d cycles taken\n",(uint)(sum));
      }

      
    }
    for(;;);
  }
}
