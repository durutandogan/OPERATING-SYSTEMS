#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>

#define MAX_CHILDREN 100

int main(int argc, char *argv[])
{
    int fd[2]; //file descriptor
    pipe(fd); //creating pipe

    int n = atoi(argv[1]);

    char *command_name = argv[2];

    struct timeval start, end;

    double exec_times[MAX_CHILDREN];

    for (int i = 0; i < n; i++)
     {
        gettimeofday(&start, NULL);

        pid_t pid = fork();

        if (pid == 0) //child
        {
            close(fd[1]);
            execlp(command_name, command_name, NULL);
        }
        else //parent
        {
            close(fd[0]);
            wait(NULL);
            gettimeofday(&end, NULL);
            exec_times[i] = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
        }
    }

    // Print exec times
    double max_time = exec_times[0];
    double min_time = exec_times[0];
    double total_time = 0;

    printf("Execution times:\n");

    for (int i = 0; i < n; i++)
    {
        printf("Child %d executed in %.2f millis\n", i + 1, exec_times[i]);

       if (exec_times[i] > max_time) //finding max
       {
            max_time = exec_times[i];
       }

      if (exec_times[i] < min_time) //finding min
      {
            min_time = exec_times[i];
      }

        total_time += exec_times[i];

    }

    printf("Max: %.2f millis\n", max_time);
    printf("Min: %.2f millis\n", min_time);
    printf("Average: %.2f millis\n", total_time / n);

    return 0;
}



//Got help from book page 118-119 and this video from the lecture https://youtu.be/yQLd2iJ9Oa0?si=3ed34a0eOvpYZ8rO
