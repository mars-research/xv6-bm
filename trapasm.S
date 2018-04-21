#include "mmu.h"
#include "param.h"
.extern cpus


.global syscall_entry
.extern panic_temp
.global sys_send_recv
.extern syscalls
.extern sysenter_dispatch

.intel_syntax noprefix
# sys_send_recv_:
# mov eax, [0xac+cpus]
# mov eax, [0x18+eax]
# mov eax, [0x44+eax]
# push eax
# jmp sys_send_recv_o

syscall_entry:
  mov  esp,[0xac+cpus]
  mov esp, [0x08+esp]
  add esp,KSTACKSIZE
  push eax
  push ecx
  call sysenter_dispatch
  pop ecx
  mov edx, [ecx]
  add ecx, 4
  sti
  sysexit
.att_syntax

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
