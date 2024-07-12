#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) 
{

    int n = atoi(argv[1]);
    pid_t pid;
    int level = 0;

    printf("Main Process ID: %d, Level: 0\n", getpid());

    for (int i = 0; i < n; i++) 
	{

        pid = fork();

        if (pid == 0) //child
	{
            level++;
            printf("Process ID: %d, Parent ID: %d, Level: %d\n", getpid(), getppid(), level);
        }
	 else //parent
	{
            wait(NULL);
	    break;
        }
    }

    return 0;
}

//got help from the book and the ps slides.
