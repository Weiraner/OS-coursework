// test.c
#include "types.h"
#include "stat.h"
#include "user.h"
int main(void)
{
    printf(1, "The process ID is: %d\n", getpid());
    exit();
}