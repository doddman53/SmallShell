/***********************************************************
* Author: Patrick Dodd
* Filename: smallsh.c
* Description: Shell writing assignment in C
* Date: 5.26.2017
***********************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

// A max of 512 arguments and 2048 characters per line are allowed
#define MAX_CHAR 2048
#define  MAX_ARG 512

int shellStatus = 0;
int sigstopflag = 0;

// Function Prototypes
void catchSIGINT(int signo);
void catchSIGTSTP(int signo);
void shellLoop();
void parseLine(char *line, int length);
void execute(char **argv);

/*************************************************************
* Function: catchSIGINT
* Parameters: int signo
* Description: This function catches a sigint, Ctrl-C and
* ends the current the current process instead of the shell
* itself
*************************************************************/
void catchSIGINT(int signo) {
	char *msg = "terminated by signal ";
	char message[50];
	memset(message, '\0', sizeof(message));
	
	// print the message along with the signal which terminated the
	// process.
	sprintf(message, "%s%i%c", msg, signo, '\n');
	write(STDOUT_FILENO, message, 24);
	fflush(stdout);
}

/*************************************************************
* Function: catchSIGTSTP
* Parameters: int signo
* Description: This function catches a sigtstp, and switches
* the ability to run processes in the background off and on.
* When the shell is initially started, background processes
* are allowed.  when the user hits Ctrl-Z, foreground-only
* mode is entered and background processes aren't allowed
* until Ctrl-Z is entered again.
*************************************************************/
void catchSIGTSTP(int signo) {
	char *message1 = "\nEntering foreground-only mode (& is now ignored)\n";
	char *message2 = "\nExiting foreground-only mode\n";

	// If the shell is in foreground/background mode, switch to
	// foreground-only mode
	if (sigstopflag == 0) {
		sigstopflag = 1;
		write(STDOUT_FILENO, message1, 50);
		fflush(stdout);
	}

	// Else, switch out of foreground-only mode
	else {
		sigstopflag = 0;
		write(STDOUT_FILENO, message2, 30);
		fflush(stdout);
	}
}



/*************************************************************
* Function: shellLoop
* Parameters: none
* Description: This function runs the shell until it is
* terminated with an "exit" command. Signal handling is set up
* here, and at the beginning of each iteration, it searches for
* background child processes and reaps them once they've
* finished. 
*************************************************************/
void shellLoop() {
	int loop = 1;

	while (1) {
		struct sigaction SIGINT_action = { 0 };
		struct sigaction SIGTSTP_action = { 0 };
		SIGINT_action.sa_handler = catchSIGINT;
		SIGTSTP_action.sa_handler = catchSIGTSTP;
		sigfillset(&SIGINT_action.sa_mask);
		//SIGINT_action.sa_flags = SA_RESTART;
		sigaction(SIGINT, &SIGINT_action, NULL);
		sigaction(SIGTSTP, &SIGTSTP_action, NULL);

		int numCharsEntered = -5; // How many chars we entered
		int currChar = -5; // Tracks where we are when we print out every char
		size_t bufferSize = 0; // Holds how large the allocated buffer is
		char *lineEntered = NULL; // Points to a buffer allocated by getline() that holds our entered string + \n + \0

		while (1) {			
			pid_t pid = -5;
			
			// Check for zombies
			pid = waitpid(-1, &shellStatus, WNOHANG);
			if (pid > 0) {
				printf("background pid %i is done: ", pid);
				fflush(stdout);

				// If child exited
				if (WIFEXITED(shellStatus)) {
					printf("exit value: %d\n", WEXITSTATUS(shellStatus));
					fflush(stdout);
				}

				// If process was terminated
				if (WTERMSIG(shellStatus)) {
					printf("terminated by signal %d\n", WTERMSIG(shellStatus));
					fflush(stdout);
				}
			}

			// Command prompt for the shell
			printf(": ");
			fflush(stdout);
			numCharsEntered = getline(&lineEntered, &bufferSize, stdin);

			if (numCharsEntered == -1)
				clearerr(stdin);
			else
				break; // Exit the loop - we've got input
		}

		// Remove the trailing \n that getline adds
		lineEntered[strcspn(lineEntered, "\n")] = '\0';

		// Parse the line
		parseLine(lineEntered, numCharsEntered);

		// Free the memory allocated by getline() or else memory leak
		free(lineEntered);
		lineEntered = NULL;
	}
}

/*************************************************************
* Function: parseLine
* Parameters: char *line, int length
* Description: This function is the "heart and soul" of this
* program. It parses each line entered by the user in search
* of various commands and files for i/o.  It contains the built
* in functions cd: change directory, exit: exits the shell and
* cleans up all allocated memory and processes, and status:
* displays the exit status or termination status of the most
* recently completed process.
*************************************************************/
void parseLine(char *line, int length) {
	
	// Loop index
	int i = 0;

	// Bool to determine if a process is background or foreground - initially false
	int isBackground = 0;

	// Variable to hold PID of background process
	pid_t backpid = -5;

	// Create variables to handle file i/o
	int infile = STDIN_FILENO, outfile = STDOUT_FILENO;

	// Create variable to handle Parent and Child process id's
	pid_t pid = -5;
	char* needle = '\0';

	// Check for "$$" in the string
	needle = strstr(line, "$$");
	char *haystack;
	char modified[256];
	memset(modified, '\0', sizeof(modified));
	if (needle != NULL) {

		// If the user enters "$$" in the input string
		if (strncmp(needle, "$$", 1) == 0) {

			// Store the string up to the first $ in haystack
			haystack = strtok(line, "$");

			// concatenate the pid to the end of haystack and add it to modified
			sprintf(modified, "%s%i", haystack, getpid());

			// Find the rest of the line
			haystack = strtok(NULL, "$");

			// If there is more in the line following the "$$"
			if (haystack != NULL) {

				// Then concatenate it to the string
				strcat(modified, haystack);
			}

			// Finally, copy the new modified string back into the line
			strcpy(line, modified);
		}
	}
	
	// Create and memset string variables for input and output files
	char input[100], output[100];
	memset(input, '\0', sizeof(input));
	memset(output, '\0', sizeof(output));

	// Built-in functions	
	char **args = malloc(MAX_ARG * MAX_CHAR);
	
	// Use spaces and endlines as delimiters
	char *endline = " \n";

	// devnull
	const char devnull[] = "/dev/null";
	
	// Get commands one by one
	char *command = strtok(line, endline);

	// If no input, return to re-prompt the user
	if (command == NULL)
		goto clean;

	// Ignore comments
	// If the first character of the line is a '#', it's a comment line
	if (line[0] == '#')
		goto clean;

	// Built-In Command 1: exit -- exit the shell
	if (strcmp(line, "exit") == 0) {
		free(args);
		exit(0);
	}

	// Built-In Command 2: cd -- change directory
	else if (strcmp(line, "cd") == 0) {

		// Search for the destination, if the user entered one
		char *destination = strtok(NULL, endline);
		
		// If nothing else was entered, go to the home directory
		if (destination == NULL)
			destination = getenv("HOME");
		
		// If an invalid destination was entered or any other
		// error occurred, print an error message
		if (chdir(destination) == -1) {
			char error[50];
			strcpy(error, "smallsh: cd: ");
			strcat(error, destination);
			perror(error);
		}
	}

	// Built-In Command 3: status -- display exit/signal value of most recent occurrence
	else if (strcmp(line, "status") == 0) {
		
		// If child process exited
		if (WIFEXITED(shellStatus)) {
			printf("exit value: %d\n", WEXITSTATUS(shellStatus));
			fflush(stdout);
		}
		
		// If process was terminated
		if (WTERMSIG(shellStatus)) {
			printf("terminated by signal %d\n", WTERMSIG(shellStatus));
			fflush(stdout);
		}
	}
	
	// Else, fork of a child process and call exec to run the command
	else {
		i = 0;
		while (command != NULL) {

			// Handle various operators
			if (strcmp(command, ">") == 0) {

				// Get the next token - this should be the filename
				command = strtok(NULL, endline);
				
				// Validate input - did the user specify a filename?
				if (command == NULL) {
					printf("error: no output file specified\n");
					fflush(stdout);
					goto clean;
				}

				// Create the filename
				strcpy(output, command);
				goto fork;				
			}
			else if (strcmp(command, "<") == 0) {

				// Get the next token - this should be the filename
				command = strtok(NULL, endline);

				// Validate input - did the user specify a filename?
				if (command == NULL) {
					printf("error: no input file specified\n");
					fflush(stdout);
					goto clean;
				}

				// Create the filename
				strcpy(input, command);				
			}

			// Check if background command was entered
			// If "echo" was entered, ignore any instance of '&' and just print it
			else if (strcmp(command, "&") == 0 && strcmp(args[0], "echo") != 0) {
				
				// Determine if in foreground mode or not
				if (sigstopflag == 1) {

					// if sigstopflag is set, ignore the &
					command = strtok(NULL, endline);
				}

				else if (sigstopflag == 0) {

					// Set the background flag to true
					isBackground = 1;

					// Get the next token - Should be NULL
					command = strtok(NULL, endline);
				}
			}

			// Place the command in the arg array, find the next command,
			// and increment the counter
			args[i] = command;
			command = strtok(NULL, endline);
			i++;
		}

		// If it's a background process and output is null,
		// redirect output to /dev/null
		if (isBackground == 1 && output[0] == '\0') {
			strcpy(output, devnull);
		}

		// If it's a background process and input is null,
		// redirect input to /dev/null
		if (isBackground == 1 && input[0] == '\0') {
			strcpy(input, devnull);
		}


	fork:

		// Add Trailing null ptr
		args[i] = NULL;
		pid = fork();

		// If it's the child process
		if (pid == 0) {

			// If background process, print and store its PID
			if (isBackground == 1) {
				backpid = getpid();
				printf("background pid is %i\n", backpid);
				fflush(stdout);
			}

			// If an output file was specified, open it
			if (output[0] != '\0') {
				outfile = open(output, O_WRONLY | O_CREAT, 0644);
				
				// If open failed, print error message and kill the process
				if (outfile < 0) {
					printf("cannot open %s for output\n", output);
					fflush(stdout);
					free(line);
					exit(1);
				}

				// Else redirect output to the file
				else {
					dup2(outfile, STDOUT_FILENO);
					close(outfile);
				}
			}

			// If an input file was specified, open it
			if (input[0] != '\0') {
				infile = open(input, O_RDONLY);

				// Error checking - does the file exist?
				// If not, print error message and kill the process
				if (infile < 0) {
					printf("cannot open %s for input\n", input);
					fflush(stdout);
					free(line);
					exit(1);
				}
				
				// Else redirect input to the file
				else {
					dup2(infile, STDIN_FILENO);
					dup2(infile, 0);

					//close(infile);
				}
			}

			// Execute the commands and exit the process
			execute(args);
			exit(0);
		}

		// If it's not a background process, it's the parent process
		// Therefore, wait
		if (isBackground != 1)
			waitpid(pid, &shellStatus, 0);
	}	

	// Clean up allocated memory and reinitialize variables
	// before the next iteration
	clean:
	free(args);
	needle = '\0';
	isBackground = 0;
}

/*************************************************************
* Function: execute
* Parameters: char **argv
* Description: Helper function used by parseLine to run exec
* commands
*************************************************************/
void execute(char **argv) {

	// If exec fails, print error message. Else, execute the
	// command
	if (execvp(*argv, argv) < 0) {
		perror(*argv);
		exit(1);
	}
}

int main() {

	// Enter the shell
	shellLoop();
	return 0;
}