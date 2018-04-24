#include "mmu.h"
#include "param.h"
.section .entrycode
.extern cpus


.global syscall_entry
.extern panic_temp
.global sys_send_recv
.extern syscalls
.extern sysenter_dispatch
.extern sysenter_dispatch_test
# sys_send_recv_:
# mov eax, [0xac+cpus]
# mov eax, [0x18+eax]
# mov eax, [0x44+eax]
# push eax
# jmp sys_send_recv_o

# %ecx contains user %esp
# %eax contains syscall number
#
# syscall_entry must preserve callee registers: EBP, EBX, EDI and ESI
# We don't touch them, hence sysenter_dispatch() will preserve them
# %esp will be restored by the sysexit
#
syscall_entry:
  movl (0xac+cpus), %esp
  movl 0x0c(%esp), %esp
#  addl KSTACKSIZE, %esp
  push %eax             # push syscall number as arg on the stack
  push %ecx             # ecx contains user-level esp
  call sysenter_dispatch
#  call sysenter_dispatch_test
  pop %ecx
  mov (%ecx), %edx      # put user-level ip into edx
  add $4, %ecx          # put user-level esp into ecx
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
