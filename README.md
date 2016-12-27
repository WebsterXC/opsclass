# OS/161 Development Kernel #
-----------------------------

### CSE421 Term Project ###

This repository represents the work I did for my Operating Systems class (CSE421) at UB.
We were given a very basic implementation of an operating system that ran on an included
simulator (simulating a MIPS r3000 processor). The entire semester was spent developing
standard operating system features in a series of assignments. For a more detailed
reference, please refer to: ops-class.org/asst/overview/

All header files reside in /kern/include

#### ASST 1: Synchronization Primitives ####

Given a spinlock implementation (implemented on a "hardware" level), we were required
to build other synchronization primitives from it. They included: locks, condition
variables, and read/write locks.

See files:
*	/kern/thread/synch.c

This repository does not have a working Read/Write lock.

#### ASST 2: System Calls and Processes ####

Given the framework for system calls (trapping has alreay been implemented), implement
various system calls found in Unix-like systems. [Note: I chose to develop OS/161 by
myself, rather than work in a group. Therefore, I was not required to implement the
file syscalls detailed in the ASST2 description. I was required to implement the
process-based syscalls (fork, execv, getpid, waitpid, exit)].

In addition to the system call interface, we were required to implement the process
subsystem to keep track of processes and their lifecycles. Similarly, the subsystem
needed to support inter-process communication.


See files:
*	/kern/proc
*	/kern/thread/thread.c
*	/kern/syscall/proc_syscalls.c	

If I recall correctly, my execv implementation leaks memory (or runs out of it when system < 768kB).

#### ASST 3: Virtual Memory ####

The last assignment, we were required to implement virtual memory. This meant designing
and implementing paging as well as configuring TLB fault handling. In addition, we
were required to design the address space abstraction from scratch, with tight bounds
on memory leakage.

With virtual memory implemented, we were required to write the sbrk() syscall and
it's implementation, malloc().

See files:
*	/kern/vm.c
*	/kern/addrspace.c

My virtual memory implementation doesn't pass all tests with 100%. I plan on redesigning
my process subsystem entirely in Spring 2017 along with making my virtual memory system
more robust, and implementing swapping to disk.
