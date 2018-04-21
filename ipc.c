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
#define diff(x, i1) *((unsigned long long *)&ret.regs[i1*2+2]) - *((unsigned long long *)&ret.regs[i1*2])
main(void)
{
  int pid;
  unsigned long long tv __attribute__((unused));
  unsigned long long sum;

  sum = 0;
  int num;
    printf(1, "ipc: starting test\n");
    pid = fork();
    if(pid<0){
      printf(1,"ipc: cannot fork\n");
      for(;;);
    }
    else{
      if(pid == 0){
        printf(1,"ipc: dispatch child process\n");
        struct msg ret __attribute__ ((aligned (64)));  
        
        recv(0,&ret);
        //unsigned long long retval = rdtsc() - *((unsigned long long *)&ret);
        for(num = 0;num<ITERS-1;num++){
        //*((unsigned long long *)&ret.regs[12]) = rdtsc();
        tv = rdtsc();
        send_recv(0, &ret);
        sum+=rdtsc()-tv;
       /* sum0+=diff(x, 0);
        sum1+=diff(x, 1);
        sum2+=diff(x, 2);
        sum3+=diff(x, 3);
        sum4+=diff(x, 4);
        sum5+= *((unsigned long long *)&ret.regs[0]) - *((unsigned long long *)&ret.regs[12]);
        sum6+=rdtsc() - *((unsigned long long *)&ret.regs[10]);*/


        //sum+=*((unsigned long long*)&ret.regs);
        }

        
        UInt64DivAndGetMod(&sum, ITERS-1);

        printf(1, "ipc: message recieved from parent: %d cycles taken\n",(uint)(sum));
        *((unsigned long long*)ret.regs) =  rdtsc();  
        send(0, &ret);
      }
      else{
        struct msg ret __attribute__ ((aligned (64)));
        sleep(50);
        
        
        for(num = 0;num<ITERS;num++){
          tv = rdtsc();
         // *((unsigned long long *)&ret.regs[12]) = rdtsc();
         send_recv(0, &ret);
         sum += rdtsc() - tv;
        /*sum0+=diff(x, 0);
        sum1+=diff(x, 1);
        sum2+=diff(x, 2);
        sum3+=diff(x, 3);
        sum4+=diff(x, 4);
        sum5+= *((unsigned long long *)&ret.regs[0]) - *((unsigned long long *)&ret.regs[12]);
        sum6+=rdtsc() - *((unsigned long long *)&ret.regs[10]);*/
        //sum+=*((unsigned long long*)&ret.regs);
        }
        
        UInt64DivAndGetMod(&sum, ITERS);
 
        printf(1, "ipc: message parent recieved back from child: %d cycles taken\n",(uint)(sum));
      }

      
  }
}
