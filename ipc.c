// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

typedef unsigned int uint32_t; 
typedef  unsigned long long uint64_t;
#define ITERS 1000000

/*
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
*/

char *argv[] = { "sh", 0 };
static __inline__ unsigned long long rdtsc(void)
{
    unsigned long long int x;
    __asm__ volatile ("rdtsc" : "=A" (x));
    return x;
}

#define diff(x, i1) *((unsigned long long *)&ret.regs[i1*2+2]) - *((unsigned long long *)&ret.regs[i1*2])

void client() {
  unsigned long i; 
  unsigned long long start, end; 
  struct msg ret __attribute__ ((aligned (64)));

  recv(0,&ret);
        
  start = rdtsc();
  for(i = 0; i < ITERS - 1; i++){
    send_recv(0, &ret);
  }

  end = rdtsc();
        
  send(0, &ret);
  printf(1, "ipc: client(): end %d, start: %d\n",
         end, start);
  printf(1, "ipc: client(): average cycles across %d runs: %d\n",
        ITERS, (unsigned long)(end - start)/ITERS);
  return;
}

void server(void){
  unsigned long i; 
  unsigned long long start, end; 
  struct msg ret __attribute__ ((aligned (64)));

  start = rdtsc();

  for(i = 0; i < ITERS; i++){
    send_recv(0, &ret);
  }
        
  end = rdtsc(); 
        
  printf(1, "ipc: server(): average cycles across %d runs: %d\n",
         ITERS, (unsigned long)(end - start)/ITERS);
}

int
main(void)
{
  int pid;
  printf(1, "ipc: starting test\n");
  
  pid = fork();
  if(pid < 0){
    printf(1,"ipc: cannot fork\n");
    for(;;);
  }

  if(pid == 0){
    printf(1,"ipc: starting client\n");
    client(); 
  } else {
    printf(1,"ipc: starting server\n");	  
    sleep(50);
    server();     
    wait();
  };

  exit();
}
