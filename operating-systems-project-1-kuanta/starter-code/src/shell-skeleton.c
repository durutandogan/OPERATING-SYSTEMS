#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
const char *sysname = "mishell";
#define MODULE "mymodule"

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n",
		   command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");

	for (i = 0; i < 3; i++) {
		printf("\t\t%d: %s\n", i,
			   command->redirects[i] ? command->redirects[i] : "N/A");
	}

	printf("\tArguments (%d):\n", command->arg_count);

	for (i = 0; i < command->arg_count; ++i) {
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	}

	if (command->next) {
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
	if (command->arg_count) {
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}

	for (int i = 0; i < 3; ++i) {
		if (command->redirects[i])
			free(command->redirects[i]);
	}

	if (command->next) {
		free_command(command->next);
		command->next = NULL;
	}

	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);

	// trim left whitespace
	while (len > 0 && strchr(splitters, buf[0]) != NULL) {
		buf++;
		len--;
	}

	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL) {
		// trim right whitespace
		buf[--len] = 0;
	}

	// auto-complete
	if (len > 0 && buf[len - 1] == '?') {
		command->auto_complete = true;
	}

	// background
	if (len > 0 && buf[len - 1] == '&') {
		command->background = true;
	}

	char *pch = strtok(buf, splitters);
	if (pch == NULL) {
		command->name = (char *)malloc(1);
		command->name[0] = 0;
	} else {
		command->name = (char *)malloc(strlen(pch) + 1);
		strcpy(command->name, pch);
	}

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1) {
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// trim left whitespace
		while (len > 0 && strchr(splitters, arg[0]) != NULL) {
			arg++;
			len--;
		}

		// trim right whitespace
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL) {
			arg[--len] = 0;
		}

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// piping to another command
		if (strcmp(arg, "|") == 0) {
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0) {
			// handled before
			continue;
		}

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<') {
			redirect_index = 0;
		}

		if (arg[0] == '>') {
			if (len > 1 && arg[1] == '>') {
				redirect_index = 2;
				arg++;
				len--;
			} else {
				redirect_index = 1;
			}
		}

		if (redirect_index != -1) {
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 &&
			((arg[0] == '"' && arg[len - 1] == '"') ||
			 (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}

		command->args =
			(char **)realloc(command->args, sizeof(char *) * (arg_index + 1));

		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;

	// increase args size by 2
	command->args = (char **)realloc(
		command->args, sizeof(char *) * (command->arg_count += 2));

	// shift everything forward by 1
	for (int i = command->arg_count - 2; i > 0; --i) {
		command->args[i] = command->args[i - 1];
	}

	// set args[0] as a copy of name
	command->args[0] = strdup(command->name);

	// set args[arg_count-1] (last) to NULL
	command->args[command->arg_count - 1] = NULL;

	return 0;
}

void prompt_backspace() {
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
	size_t index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &=
		~(ICANON |
		  ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	show_prompt();
	buf[0] = 0;

	while (1) {
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		// handle tab
		if (c == 9) {
			buf[index++] = '?'; // autocomplete
			break;
		}

		// handle backspace
		if (c == 127) {
			if (index > 0) {
				prompt_backspace();
				index--;
			}
			continue;
		}

		if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
			continue;
		}

		// up arrow
		if (c == 65) {
			while (index > 0) {
				prompt_backspace();
				index--;
			}

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}

	// trim newline from the end
	if (index > 0 && buf[index - 1] == '\n') {
		index--;
	}

	// null terminate string
	buf[index++] = '\0';

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main() {
	while (1) {
		struct command_t *command = malloc(sizeof(struct command_t));

		// set all bytes to 0
		memset(command, 0, sizeof(struct command_t));

		int code;
		code = prompt(command);
		if (code == EXIT) {
			break;
		}

		code = process_command(command);
		if (code == EXIT) {
			break;
		}

		free_command(command);
	}


	printf("\n");
	return 0;
}

// PART 4
int get_parent_pid(int pid){
    char path[256];
    FILE *file;
    int ppid = 0;

    sprintf(path, "/proc/%d/stat", pid);
    file = fopen(path, "r");
    if (!file) return -1;

    fscanf(file, "%*d %*s %*c %d", &ppid);
    fclose(file);
    return ppid;
}


void draw_graph(int root_pid, const char* out_file) {
    DIR* dir = opendir("/proc");
    struct dirent* entry;
    FILE* image_file; //output file

    char dot_file[] = "graph.dot";
    image_file = fopen(dot_file, "w"); //open the image file
    if (!image_file) {
        perror("Failed to open dot file");
        return;
    }

    fprintf(image_file, "digraph G {\n");

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
            int pid = atoi(entry->d_name);
            int ppid = get_parent_pid(pid);
            if (ppid == root_pid) {
                fprintf(image_file, "    %d -> %d;\n", ppid, pid);
            }
        }
    }

    fprintf(image_file, "}\n");
    fclose(image_file);
    closedir(dir);

    char command[256];
    snprintf(command, sizeof(command), "dot -Tpng %s -o %s", dot_file, out_file);
    system(command);
}

int process_command(struct command_t *command) {

	int r;
	int WRITE_END = 1;
        int READ_END = 0;
        char *inspiration_quotes[] = {
	      "Your self-worth is determined by you. You don't have to depend on someone telling you who you are. — Beyonce" ,
	      "Nothing is impossible. The word itself says 'I'm possible!' — Audrey Hepburn",
              "Keep your face always toward the sunshine, and shadows will fall behind you. — Walt Whitman ",
              "You have brains in your head. You have feet in your shoes. You can steer yourself any direction you choose. You're on your own. And you know what you know. And you are the guy who'll decide where to go. — Dr. Seuss",
              "To bring about change, you must not be afraid to take the first step. We will fail when we fail to try. — Rosa Parks",
              "All our dreams can come true, if we have the courage to pursue them. — Walt Disney",
              "Don't sit down and wait for the opportunities to come. Get up and make them. — Madam C.J. Walker",  
              "Champions keep playing until they get it right. — Billie Jean King",
              "I am lucky that whatever fear I have inside me, my desire to win is always stronger. — Serena Williams",
              NULL
	};

	if (strcmp(command->name, "") == 0) {
		return SUCCESS;
	}

	if (strcmp(command->name, "exit") == 0) {
		return EXIT;
	}

	if (strcmp(command->name, "cd") == 0) {
		if (command->arg_count > 0) {
			r = chdir(command->args[1]); // changed the arg index to match the desired directory
			if (r == -1) {
				printf("-%s: %s: %s\n", sysname, command->name,
					   strerror(errno));
			}
			else {
			printf("changed directory successfully\n"); // print statement added to see successful action
			}

			return SUCCESS;
		}
	}
	
	if (strcmp(command->name, "hdiff") == 0) {
	    // initially checking what version does the inputted command match
	    if (strcmp(command->args[1], "-b") == 0) {
	      // create necessary variables 
	      char const* const fn1 = command->args[2];
	      char const* const fn2 = command->args[3];
	      unsigned long long byteDiff = 0; 
	      int b1,b2;
	      
	      // opening both files to read, specifying the binary file reading subscript
	      FILE* f1 = fopen(fn1, "rb"); 
	      FILE* f2 = fopen(fn2, "rb");
	      if (fn1 == NULL || fn2 == NULL) {
                perror("Failed to open file/s");
                return UNKNOWN;
                }
              // there will be 3 conditions:
              // while their sizes are compatible, first while loop will run and calculate the difference between each unit step by step 
             // if one of them reaches the end of the file, meaning one is shorter than the other, according to the file which did not reach to the end yet; 
             // second or third loop will run and will add each part to the byte difference directly 
              while ((b1 = fgetc(f1)) != EOF && (b2 = fgetc(f2)) != EOF) {
                byteDiff += abs(b1 - b2);
                }
               while ((b1 = fgetc(f1)) != EOF) {
                  byteDiff += abs(b1); 
                }
              while ((b2 = fgetc(f2)) != EOF) {
                  byteDiff += abs(b2); 
               }
               
               // close the files 
               fclose(f1);
               fclose(f2);
                
            if (byteDiff == 0) {
              printf("The two files are identical\n");
            }
            else {
	    printf("The two files are different in %llu bytes\n", byteDiff);
	    }
	    return SUCCESS;
	    }
	    
	    // everything else are taken as hdiff -a by default, since it was stated in the pdf 
	    else { 
	      // if no arguments are given other than hdiff arg1 and arg2 will correspond to the file names
	      int arg1 = 1;
	      int arg2 = 2;
	      if (strcmp(command->args[1], "-a") == 0) {
	        // else file names will be one further
	        arg1 = 2;
	        arg2= 3;
	      }
	      
	      // create the necessary variables
	      char const* const fileName1 = command->args[arg1];
	      char const* const fileName2 = command->args[arg2];
	      FILE* file1 = fopen(fileName1, "r"); 
	      FILE* file2 = fopen(fileName2, "r");
	      
	      if(!file1 || !file2){
                  printf("Unable to open files or you entered a wrong command.\n"); 
                  return UNKNOWN;
                  }
              // create char arrays to enable line by line reading
	      char line1[500];
	      char line2[500];
	      // create counters to keep track of the line number
	      int counter1 = 1;
	      int counter2 = 1;
	      // create var to count different occurences
	      int differ = 0;
	      
	      // read the lines 
	      while (fgets(line1, sizeof(line1), file1)) {
              
              // if file 2 ended but file 1 still has lines, handle it in here
	      if (fgets(line2, sizeof(line2), file2) == NULL) {
	          printf("%s:Line %d: %s", fileName1, counter1, line1);
	          differ++;
	      }
	      
	      // if both files lines are not null, compare them and if they are not equal print them and increase
	      // the difference counter
	      else if (strcmp(line1, line2) != 0) {
	            printf("%s:Line %d: %s", fileName1, counter1, line1);
	            printf("%s:Line %d: %s", fileName2, counter2, line2);
	            differ++;
	          
	          }
	      counter1++;
	      counter2++;
	        
	      }
	      
	      // if file 1 ended and file 2 still has lines to read, handle it in here
	      while (fgets(line2, sizeof(line2), file2)) {
	        printf("%s:Line %d: %s", fileName2, counter2, line2);
	        counter2++;
	        differ++;
	      
	      }
	      
	      
	      if (differ==0) {
	        printf("The two files are identical\n");
	      }
	      else {
	      printf("%d different line/s found\n", differ);
	      }
	      return SUCCESS;
      
	    }
	    
	    return SUCCESS;
	}
	// PART 3 C
	// Mislina's command:
	if (strcmp(command->name, "dog_encrypt") == 0) {
	    if (strcmp(command->args[1], "-h") == 0) {
	    printf("Welcome to the dog encryptor! This dog will get your input word and find the Caesar Cipher encrypted version of your input with a random number of shifts, enabling an encrypted password only between you and our dog! To get the shift number add -n to the beginning of your word.'");
	    return UNKNOWN;
	    }
	    int shift = rand() % 26; 
	    int arg_num = 1;
	     if (strcmp(command->args[1], "-n") == 0) {
	        arg_num = 2;
	        printf("Random shift number: %d\n", shift);
	    }
	    
	    char *text = command->args[arg_num];
	    for (int i = 0; text[i] != '\0'; i++) {
             if (isalpha(text[i])) {  
                char base = 'A';  
                if (islower(text[i])) {
                    base = 'a';  
            }
            text[i] = (text[i] - base + shift) % 26 + base;  
        }
    }
	   
	    
	    printf("    ______\n");
    printf("  /      \\\n");
    printf("  %s   \n", text);
    printf("  \\______/\n");
    printf("     |\n");
    printf("     |\n");
    printf("  / \\__\n");
    printf(" (    @\\___\n");
    printf(" /         O\n");
    printf("/   (_____/\n");
    printf("/_____/   U\n");
    return SUCCESS;

	}
	// Duru's command
	if (strcmp(command->name, "inspire") == 0){
	    srand(time(NULL));
	    
	    int size = 0;
	    while (inspiration_quotes[size] != NULL){
	        size++; //find the size of the quotes array
	    }
	
	    //selecting quote
	    int index  = rand() % size;
	    printf("%s\n", inspiration_quotes[index]);
	    
	    return SUCCESS; 
	}
	
	if (strcmp(command->name, "psvis") == 0){
	      //takes 3 args : command , pid, output file
	     
	      int pid = atoi(command->args[1]);
	      char *output_file = command->args[2];
	      
	      //load unload kernel module???
	      //generating image
	      draw_graph(pid, output_file);
	      
              return SUCCESS;
      
	}
	
	
	// create the pipe for part 2
	int fd[2];  
	pipe(fd);  


	pid_t pid = fork();
	// child
	if (pid == 0) {
		/// This shows how to do exec with environ (but is not available on MacOs)
		// extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// TODO: do your own exec with path resolving using execv()
		// do so by replacing the execvp call below

		// PART 3.A
		if (command->auto_complete) {
		
		// transforming the format of the input to extract the written
		  char *input = command->name;
		  
		  int j, n = strlen(input);
                  for (int i = j = 0; i < n; i++)
                     if (input[i] != '?')
                       input[j++] = input[i];
 
                 input[j] = '\0';

    
		  
     
    // getting the path and parsing tokens of it
    char *path = getenv("PATH");
    char *dirs = strtok(path, ":");
    bool found = false;
    while (dirs != NULL) {
        // checking each directory in the path 
        DIR *dir = opendir(dirs);
        if (dir == NULL) {
            // parse tokens
            dirs = strtok(NULL, ":");
            continue;
        }
        // check inside of all directories to see the matching ones with the input
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, input, strlen(input)) == 0) {
                if (!found) {
                    printf("\n"); 
                    found = true;
                }
                // print the matched entry with the inputted command  
                printf("%s   ", entry->d_name);
            }
        }
        closedir(dir);
        dirs = strtok(NULL, ":");
    }

    if (found) {
        printf("\n");
        show_prompt();  
        printf("%s", input);  
    }
} 
	    
	      
		execvp(command->name, command->args); // exec+args+path
		
		// PART 2
		// this will act as the flag of the pipe occurence
		if(command->next){
		    // close the read end of the file and get the output of the current command and write it to the write end of the pipe
                    close(fd[READ_END]);
                    int curr = dup(STDIN_FILENO);
                    dup2(fd[WRITE_END], STDIN_FILENO);
                    close(fd[WRITE_END]);

                    // recursively call the next command to handle multiple pipes
                    process_command(command->next); //call for next one
                    close(curr);
                }
                else{
                    close(fd[READ_END]);
                }
		
		exit(0);
	} else {
                
                
                // background handling
                // if background flag is not satisfied, wait for the current child process to end
                if(!command->background){
		  waitpid(pid, NULL, 0);
		  
		}
	      
	        // if process is running in the background just return it 
	        else {
            printf("Process [%d] running in background.\n", pid);
        }
                return SUCCESS;
	}

	// TODO: your implementation here

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
