Name: Shiya Su
cs login: shiya
wiscID: ssu56
emial: ssu56@wisc.edu

I have passed all three tests

Files modified:
Makefile: add name of program test.c and getparentname.c
defs.h: declare int getparentname()
proc.c: define int getparentname()
syscall.c: import extern int sys_getparentname(void) and declare [SYS_getparentname] sys_getparentname
syscall.h: define SYS_getparentname = 22
sysproc.c: define sys_getparentname()
user.h: declare int getparentname()
usys.S: declare system call SYSCALL(getparentname)
