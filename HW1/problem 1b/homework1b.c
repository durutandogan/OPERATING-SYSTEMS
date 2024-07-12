#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main()
{
    pid_t pid = fork();

    if (pid == 0) 
    {
        //if child exit
        exit(0);
    }
    else
    {
        // parent sleep for 5 seconds
        printf("Child PID: %d (Zombie for 5 seconds)\n", pid);
        sleep(5);
    }

    return 0;
}
