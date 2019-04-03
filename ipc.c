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

void test_int_null() {
  unsigned long i; 
  unsigned long long start, end; 

        
  start = rdtsc();
  for(i = 0; i < ITERS; i++){
    int_null();
  }

  end = rdtsc();
        
  printf(1, "overhead of int average cycles %d across runs: %d\n",
        ITERS, (unsigned long)(end - start)/ITERS);
  return;
}

void test_sysenter_null() {
  unsigned long i; 
  unsigned long long start, end; 

        
  start = rdtsc();
  for(i = 0; i < ITERS; i++){
    sysenter_null();
  }

  end = rdtsc();
        
  printf(1, "overhead of sysenter average cycles %d across runs: %d\n",
        ITERS, (unsigned long)(end - start)/ITERS);
  return;
}

void test_cr3_reload() {
  unsigned long i; 
  unsigned long long start, end; 
  struct msg m __attribute__ ((aligned (64)));

        
  start = rdtsc();
  for(i = 0; i < ITERS; i++){
    cr3_reload(0, &m);
  }

  end = rdtsc();
        
  printf(1, "overhead of cr3_reload across %d runs: average cycles %d\n",
        ITERS, (unsigned long)(end - start)/ITERS);
  return;
}

void test_touch_pages(){
  unsigned long i;
  unsigned long long start, end;
  
  start = rdtsc();
  for(i = 0; i < ITERS; i++){
    touch_pages();
  }
  end = rdtsc();

  printf(1, "overhead of touch_pages across %d runs: average cycles %d\n",
        ITERS, (unsigned long)(end - start)/ITERS);

  return;
}

void test_send_recv_dummy() {
  unsigned long i; 
  unsigned long long start, end; 
  struct msg m __attribute__ ((aligned (64)));

        
  start = rdtsc();
  for(i = 0; i < ITERS; i++){
    send_recv_dummy(0, &m);
  }

  end = rdtsc();
        
  printf(1, "overhead of send_recv_dummy average cycles %d across runs: %d\n",
        ITERS, (unsigned long)(end - start)/ITERS);
  return;
}
void client() {
  unsigned long i; 
  //unsigned long long start, end; 
  struct msg m __attribute__ ((aligned (64)));

  //oops();

  recv(0,&m);
        
  //start = rdtsc();
  for(i = 0; i < ITERS - 1; i++){
    send_recv(0, &m);
  }

  //end = rdtsc();
        
  send(0, &m);

  //oops(); 

/*  printf(1, "just a check\n"); 

  printf(1, "ipc: client(): end %d, start: %d\n",
         (unsigned long)end, (unsigned long)start);
  printf(1, "ipc: client(): average cycles across %d runs: %d\n",
        (unsigned long) ITERS, (unsigned long)(end - start)/ITERS); */
  return;
}

void server(void){
  unsigned long i; 
  unsigned long long start, end; 
  struct msg m __attribute__ ((aligned (64)));

  send_recv(0, &m);
  start = rdtsc();

  for(i = 0; i < ITERS - 2 ; i++){
    send_recv(0, &m);
  }
        
  end = rdtsc(); 
  send_recv(0, &m);
        
  printf(1, "ipc: server(): average cycles across %d runs: %d\n",
         ITERS, (unsigned long)(end - start)/ITERS);
}

int
main(void)
{
  int i; 
  int pid;
  printf(1, "ipc: starting test\n");

  //test_int_null();
  test_sysenter_null();
  //test_pgdir();
  
  for (i = 0; i < 128; i++) {
    test_size(i);
    printf(1, "touch %d pages:", i);
    //test_touch_pages();
    test_cr3_reload();
  }


  //test_send_recv_dummy();
 
#if 1  
  pid = fork();
  if(pid < 0){
    printf(1,"ipc: cannot fork\n");
    for(;;);
  }

  if(pid == 0){
    //printf(1,"ipc: starting client\n");
    client(); 
  } else {
    //printf(1,"ipc: starting server\n");	  
    sleep(1);
    server();     
    wait();
  };
#endif

  exit();
}
