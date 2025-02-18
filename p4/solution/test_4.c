#include "types.h"
#include "stat.h"
#include "user.h"
#include "pstat.h"
#include "test_helper.h"

int main(int argc, char *argv[])
{
    struct pstat ps;
    int run_ticks = 30;
    int sleep_ticks = 50;

    int pid1 = fork();

    if (pid1 == 0)
    {                             // proc1
        run_until(run_ticks * 4); // Ensure it runs throughout the test
        exit();
    }
    else if (pid1 > 0)
    {
        int pid2 = fork();

        if (pid2 == 0)
        { // proc2
            run_until(run_ticks);

            if (getpinfo(&ps) != 0)
            {
                printf(1, "getpinfo failed before sleep\n");
                exit();
            }

            int my_idx = find_my_stats_index(&ps);
            int old_pass = ps.pass[my_idx];
            int old_remain = ps.remain[my_idx];

            sleep(sleep_ticks);

            if (getpinfo(&ps) != 0)
            {
                printf(1, "getpinfo failed after sleep\n");
                exit();
            }

            my_idx = find_my_stats_index(&ps);
            int proc1_idx = find_stats_index_for_pid(&ps, pid1);

            ASSERT(ps.rtime[proc1_idx] > 0,
                   "Proc1 didn't get runtime while proc2 slept");

            ASSERT(ps.pass[my_idx] != old_pass,
                   "Pass value did not change after sleep");
            ASSERT(ps.remain[my_idx] != old_remain,
                   "Remain value was not updated after sleep");

            exit();
        }
        else if (pid2 > 0)
        { // Parent process
            wait();
            wait();

            // Get final stats
            if (getpinfo(&ps) != 0)
            {
                printf(1, "getpinfo failed in parent\n");
                exit();
            }

            int proc1_idx = find_stats_index_for_pid(&ps, pid1);
            int proc2_idx = find_stats_index_for_pid(&ps, pid2);

            ASSERT(ps.rtime[proc1_idx] > 0 && ps.rtime[proc2_idx] > 0,
                   "Both processes should have non-zero runtime");

            ASSERT(ps.pass[proc1_idx] > ps.pass[proc2_idx],
                   "Proc1 should have higher pass value due to more runtime");

            test_passed();
            exit();
        }
    }

    printf(1, "Fork failed\n");
    exit();
}