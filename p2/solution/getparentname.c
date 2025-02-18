#include "types.h"
#include "stat.h"
#include "user.h"
int main(void)
{
    char *a = malloc(sizeof(char) * 16);
    char *b = malloc(sizeof(char) * 16);

    if (getparentname(a, b, 2, 2) < 0)
    {
        exit();
    }
    printf(1, "XV6_TEST_OUTPUT Parent name: %s Child name: %s\n", a, b);
    exit();
}