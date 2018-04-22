#include "mmu.h"
#include "param.h"
.extern cpus


.global syscall_entry
.extern panic_temp
.global sys_send_recv
.extern syscalls
.extern sysenter_dispatch
# sys_send_recv_:
# mov eax, [0xac+cpus]
# mov eax, [0x18+eax]
# mov eax, [0x44+eax]
# push eax
# jmp sys_send_recv_o


syscall_entry:
  movl  (0xac+cpus), %esp
  movl 0x0c(%esp), %esp
#  addl KSTACKSIZE, %esp
  push %eax
  push %ecx
  call sysenter_dispatch
  pop %ecx
  mov (%ecx), %edx
  add $4, %ecx
  sti
  sysexit

  # vectors.S sends all traps here.
.globl alltraps
alltraps:
  # Build trap frame.
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal
  
  # Set up data segments.
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es

  # Call trap(tf), where tf=%esp
  pushl %esp
  call trap
  addl $4, %esp

  # Return falls through to trapret...
.globl trapret
trapret:
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  addl $0x8, %esp  # trapno and errcode
  iret
