#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define MAX_LINE 80

/*
	This program uses no user-defined functions and is contained withing main.
	The user inputs a command and it is executed in the pseud-shell either
	by simply using the execvp() command or by making some minor alterations
	to the proccess if any operator is included (i.e. <, >, |). All the operators
	are marked by comments, ~line 61 is the start of left and right (<, >) handeling,
	~line 97 is the start of bar (|) handeling, and basic command handeling starts
	on ~line 137.
*/

int main(void) {
	char* args[MAX_LINE / 2 + 1];
	int should_run = 1;

	while (should_run) {				// matching project specs until line 42, not really my own work
		char in[MAX_LINE], prev_cmd[MAX_LINE];
		int concurrent = 0, index = -1, has_cmd;

		printf("osh> ");
		fflush(stdout);
		fgets (in, sizeof(in), stdin);
		has_cmd = 1;

		if (strcmp(in, "exit") == 10)
			should_run = 0;
		else if (strcmp(in, "!!") == 10) {
			if (strcmp(&prev_cmd[0], "\0") != 56) {			// make sure that previous cmd is not empty
				strcpy(in, prev_cmd);
				printf("previous command: %s", prev_cmd);
			}
			else { 
				printf("No commands in history.\n");
				has_cmd = 0;
			}
		}
		if (should_run && has_cmd) {		// check if its still running and if "!!" wasn't input with no previous command
			pid_t child = fork();
			if (child == 0) {
				char* unparsed = strtok(in, " ");
				index = 0;
				while (unparsed != NULL) {			// this loop simply parses the user input by spacing and also removes trailing new lines that gave me headaches
					if (unparsed[strlen(unparsed)-1] == '\n')
						unparsed[strlen(unparsed)-1] = 0;
					args[index] = unparsed;
					unparsed = strtok(NULL, " ");
					index++;
				}
				args[index] = NULL;
				int argc = index - 1;

				// left and right check
				int fd[2] = { -1, -1 };
				if (strcmp(args[argc - 1], ">") == 0) {
					fd[1] = open(args[argc], O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);	// the 2nd argument in open was found online, not my own work.
					if (fd[1] == -1) {
						perror("open");
					}
					args[argc - 1] = NULL;		// redefine the args array ending
				}
				else if (strcmp(args[argc - 1], "<") == 0) {
					fd[0] = open(args[argc], O_RDONLY);		// open the file to be read
					if (fd[0] == -1) {
						perror("open");
					}
					args[argc - 1] = NULL;		// redefine the args array ending
				}
				if (fd[0] != -1 || fd[1] != -1) {
					if (fd[0] != -1) {					// if there is a "<"
						if (dup2(fd[0], STDIN_FILENO) != STDIN_FILENO) {
							printf("line 78, ");
							perror("dup2");
						}
					}
					else if (fd[1] != -1) {					// if there is a ">"
						if (dup2(fd[1], STDOUT_FILENO) != STDOUT_FILENO) {
							printf("line 83, ");
							perror("dup2");
						}
					}
					execvp(args[0], args);
					perror("execvp");
				}
				// left and right complete

				// bar
				int bar = -1;
				for (int index = 0; index < argc; index++) {	// find if there is a "|" operator and if so at what index
					if (strcmp(args[index], "|") == 0)
						bar = index;
				}
				if (bar != -1) {
					int p[2];		// 0 = read, 1 = write
					pid_t c1, c2;	// 2 children spawned, one for each cmd
					
					pipe(p);
					c1 = fork();
					if (c1 == 0) {
						c2 = fork();
						if (c2 == 0) {	// 1st cmd
							char* cmds[bar + 1];
							dup2(p[1], 1);
							for (int index = 0; index < bar; index++) {			// parse this half of the cmd from the original args array
								cmds[index] = (char*) malloc(strlen(args[index]) * 8);
								cmds[index] = args[index];
							}
							cmds[bar] = NULL;
							if (execvp(cmds[0], cmds) < 0)
								printf("  failed to execute child 1 command...\n");
						}
						else {	// 2nd cmd
							char* cmds[argc - bar + 1 + 1];
							int cmd_index = 0;
							for (int index = bar + 1; index < argc; index++) {			// parse this half of the cmd from the original args array
								cmds[cmd_index] = (char*) malloc(strlen(args[index]) * 8);
								cmds[cmd_index] = args[index];
								cmd_index++;
							}
							waitpid(c2, NULL, 0);
							close(p[1]);
							if (execvp(cmds[0], cmds) < 0)			// problem here (mentioned in README)
								printf("  failed to execute child 2 command...\n");
						}
					}
					else
						waitpid(c1, NULL, 0);
				}
				// bar complete
				else if (bar == -1 && fd[0] == -1 && fd[1] == -1) {
					if (execvp(args[0], args) < 0)
						printf("  failed to execute basic command...\n");
				}
			}
			else {
				waitpid(child, NULL, 0);
			}
		}
		strcpy(prev_cmd, in);
	}
}