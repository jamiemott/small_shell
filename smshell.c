/* Small Shell
 * Author: Jamie Mott
 * Date: February 25, 2020
 */


//Library inclusions
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>		
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>					
#include <string.h>
#include <stdbool.h>		
#include <fcntl.h>
#include <errno.h>
#include <signal.h>


//Constants
#define MAXCHAR 2048
#define MAXLEN 6144	
#define MAXARGS 512


//Global variables
bool backgroundAllowed = 1;
bool runShell = 1;

int inFileCheck;
int outFileCheck;
int bgCheck; 
int bgIndex;
int inputSize;
int statusCode = 0;
int kids[250];

char input[MAXCHAR];
char inFile[MAXCHAR];
char outFile[MAXCHAR];
char *args[MAXARGS];


/*This function takes the prompt input, checks it for needed expansion, input and output
 *redirection necessary and then splits the input into individual arguments to be used in
 *the main shell program.
 */
void splitArgs()
{
	char extendedInput[MAXLEN];
	char tempInput1[MAXLEN];
	char tempInput2[MAXLEN];
	char *token;
	int i, j, restart;

	//Copy the user input into a bigger string to allow for expansion
	strcpy(extendedInput, input);

	//Check to see if there is a need to expand process ID
	for (i = 0; i < inputSize - 1; i++)
	{
		if (extendedInput[i] == '$' && extendedInput[i + 1] == '$')
		{
			restart = i - 1; //Hold index for checking the rest later in case there are more $$s
			
			//Make a copy of the string so we can manipulate
			strcpy(tempInput1, extendedInput);
			int pid = getpid();
			tempInput1[i] = '\0';

			snprintf(tempInput2, sizeof(tempInput2), "%s%d", tempInput1, pid);

			i += 2; //skip over $$ to continue checking input line

			if (extendedInput[i] != '\0')
			{
				j = 0;
				for (; i < inputSize; i++)
				{
					tempInput1[j] = extendedInput[i];
					j++;
				}
				tempInput1[j] = '\0';

				snprintf(extendedInput, sizeof(extendedInput), "%s%s", tempInput2, tempInput1);

				inputSize = strlen(extendedInput);
				i = restart;
			}

			else
			{
				strcpy(extendedInput, tempInput2);
			}			
		}
	}

	/*Break input apart with strtok and place each item into args array. Source:
	 *www.geeksforgeeks.org/strtok-strtok_r-functions-c-examples
	 */
	i = 0;
	token = strtok(extendedInput, " ");
	args[i] = token;

	while (token != NULL && i != MAXARGS)
	{
		token = strtok(NULL, " ");
		args[++i] = token;
	}

	int lastArg = i - 1;		//Save this index to use in later argument checking

	//Check for background indicator. If present, set flag and remove from input
	if (strcmp(args[lastArg], "&") == 0)
	{
		bgCheck = 1;
		args[lastArg] = '\0';
	}

	//Look for redirection indicators and save file names
	for (j = 0; j < lastArg; j++)
	{
		if (strcmp(args[j], "<") == 0)  //Input file redirection
		{
			inFileCheck = 1;
			strcpy(inFile, args[j + 1]);
		}

		else if (strcmp(args[j], ">") == 0)  //Out file redirection
		{
			outFileCheck = 1;
			strcpy(outFile, args[j + 1]);
		}
	}

	//If there were files, reset end of the arguments so they don't get passed into exec()
	if (inFileCheck == 1)
	{
		args[lastArg - 1] = '\0';
		lastArg -= 2;
	}

	if (outFileCheck == 1)
	{
		args[lastArg - 1] = '\0';
		lastArg -= 2;
	}
}


/*catchSIGTSTP: Function changes the setting of the modes allowed: foreground only
 *vs. both when Ctrl Z is recieved.  The function prints a message letting the user know which
 *mode they are currently in.
 */
void catchSIGTSTP(int signo)
{
	//Background not currently allowed. Switch and let user know.
	if (backgroundAllowed == 0)
	{
		backgroundAllowed = 1;
		//Source: www.geeksforgeeks.org/input-output-system-calls-c-create-open-close-read-write/
		write(1, "Exiting foreground only mode.\n: ", strlen("Exiting foreground only mode.\n: "));
	}

	else
	{
		backgroundAllowed = 0;
		write(1, "Foreground only mode. & will be ignored.\n: ", strlen("Foreground only mode. & will be ignored.\n: "));
	}
}


/*Main driver program. Gets user input, calls splitArgs to parse the input into command statements and
 *then runs commands as provided. Spilts of child processes in forground and background as allowed and
 *needed. Kills all child processes before exiting when usesr types "exit" otherwise loops to take more input.
 *Also sets up signal handlers.
 */
int main()
{	
	int i;

	//Initialize list of child process IDs to a signal value for later checking
	for (i = 0; i < 250; i++)
	{
		kids[i] = -5;
	}
	
	//Ignore Ctrl C in main shell and background
	struct sigaction SIGINT_action = { 0 };
	SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&SIGINT_action.sa_mask);
	sigaction(SIGINT, &SIGINT_action, NULL);

	//Redirect Ctrl Z. Need to catch code catchSIGTSTP so that it only displays after foreground processes
	struct sigaction SIGTSTP_action = { 0 };
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigfillset(&SIGTSTP_action.sa_mask);
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	/*Need to create a sigset in order to block the parent from calling the SIGTSTP handler while
	 *foreground process is running. Source: www.stackoverflow.com/questions/25261/set-and-oldset-in-sigprocmask 
	 */
	sigset_t stopTSTP;
	sigemptyset(&stopTSTP);
	sigaddset(&stopTSTP, SIGTSTP);
	

	//Main shell loop. Runs until user types "exit"
	while (runShell == 1)
	{	
		printf(": ");
		fflush(stdout);

		//Get user input. Do initial check for blanks and comments.
		fgets(input, MAXCHAR, stdin);
		input[strcspn(input, "\n")] = '\0';
		inputSize = strlen(input);

		if (inputSize == 0 || input[0] == '#' || input[0] == ' ')
		{
			continue;
		}
				
		else
		{
			inFileCheck = outFileCheck = bgCheck = -1;

			//Take input, expand $$ as needed, check for filestream redirection and background indicators
			splitArgs();
			
			//Check for the shell's three build in commands
			if (strcmp(args[0], "exit") == 0)  //Kill processes and exit shell
			{
				//Set while loop to end
				runShell = 0;
				
				/*Kill off any remaining background processes. Source:
				 *www.stackoverflow.com/questions/6501522/how-to-kill-a-child-process-by-the-parent-process/
				 */
				for (i = 0; i < 250; i++)
				{
					if (kids[i] != -5)
					{
						kill(kids[i], SIGKILL);
					}
				}
			}

			else if (strcmp(args[0], "cd") == 0)  //Change directory to home or specified path
			{
				/*Moving to home directory. Source:
				 *www.stackoverflow.com/questions/9493234/chdir-to-home-directory
				 */
				if (args[1] == NULL) //No other commands
				{
					chdir(getenv("HOME"));
				}

				else  //Move to specified directory, pop error if bad command
				{
					if (chdir(args[1]) == -1)
					{
						printf("%s directory does not exist\n", args[1]);
						fflush(stdout);
					}
				}				
			}

			/*Status code variable will be set by fork code for foreground process, but starts at 0 when 
			 *program begins. Doing it this way ignores the built in commands.
			 */
			else if (strcmp(args[0], "status") == 0)
			{
				//Terminated normally
				if (statusCode < 2)
				{
					printf("exit value %d\n", statusCode);
					fflush(stdout);
				}

				else //Terminated by signal
				{
					printf("terminated by signal %d\n", statusCode);
					fflush(stdout);
				}
			}
			
			//Not a built-in command, so fork and run if possible
			else
			{
				//Need to block the parent from being able to act on SIGTSTP until foreground process ends
				sigprocmask(SIG_BLOCK, &stopTSTP, NULL);

				pid_t childPID = -5;
				int childExitStatus = -5;
								
				childPID = fork();
				
				if (childPID == -1)
				{
					perror("Problem Child!\n");
					exit(1);
				}

				//Fork successful. Check for foreground/background running and call as appropriate
				else if (childPID == 0)
				{
					//Only allow Ctrl C for foreground processes. Ignore Ctrl Z on foreground
					if (bgCheck == -1 || backgroundAllowed == 0)
					{
						//Reset Ctrl C to allow termination
						SIGINT_action.sa_handler = SIG_DFL;
						sigaction(SIGINT, &SIGINT_action, NULL);

						//Reset Ctrl Z to ignore
						SIGTSTP_action.sa_handler = SIG_IGN;
						sigaction(SIGTSTP, &SIGTSTP_action, NULL);
					}

					//Handle any file redirection needed for child
					int inFileDesc, outFileDesc;
					
					//Check to see if background is allowed right now
					if (backgroundAllowed == 1)
					{
						//Process in background, send to dev/null if no other file direction given
						if (bgCheck == 1 && inFileCheck != 1)
						{
							//www.stackoverflow.com/questions/4263173/redirecting-stdin-stdout-stderr-to-dev-null-in-c
							inFileDesc = open("/dev/null", O_RDONLY);

							//Source: www.man7.org/linux/man-pages/man2/dup.2.html
							if (dup2(inFileDesc, STDIN_FILENO) < 0)
							{
								fprintf(stderr, "Error with bg file stream redirection\n");
								exit(1);
							}
						}

						//No designated file, send to dev/null
						if (bgCheck == 1 && outFileCheck != 1)
						{
							outFileDesc = open("/dev/null", O_WRONLY);

							if (dup2(outFileDesc, STDOUT_FILENO) < 0)
							{
								fprintf(stderr, "Error with bg file stream redirection\n");
								exit(1);
							}
						}
					}
					
					//File redirection needed.  Source: www.man7.org/linux/man-pages/man2/dup.2.html
					if (inFileCheck == 1 || outFileCheck == 1)
					{
						if (inFileCheck == 1) //Infile redirection needed
						{
							inFileDesc = open(inFile, O_RDONLY);

							if (inFileDesc < 0) //Bad file
							{
								fprintf(stderr, "cannot open %s for input\n", inFile);
								exit(1);
							}

							if (dup2(inFileDesc, STDIN_FILENO) < 0)  //Error redirecting
							{
								fprintf(stderr, "Error with input stream redirection\n");
								exit(1);
							}
							close(inFileDesc);
						}
							
						if (outFileCheck == 1)  //Outfile redirection needed
						{
							//Ideal Read/Write permissions: www.forums.cpanel.net/threads/why-are-644-and-755-unix-permissions-ideal-for-files-directories-in-public-folders.136821/
							outFileDesc = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

							if (outFileDesc < 0) //Bad file
							{
								fprintf(stderr, "cannot open %s for output\n", outFile);
								exit(1);
							}

							if (dup2(outFileDesc, STDOUT_FILENO) < 0)  //Error redirecting
							{
								fprintf(stderr, "Error with output stream redirection\n");
								exit(1);
							}
								
							close(outFileDesc);
						}
					}

					//Run command if we can, check for return code from execvp
					if (execvp(args[0], args) < 0)
					{
						/*This will print if a bad command is entered. If command is good, this will be
						*skipped when the child returns 0 from execvp
						*/
						fprintf(stderr, "%s: no such file or directory\n", args[0]);
						exit(1);
					}
				}

				//Parent process
				else
				{
					//Background is allowed and requested, do not wait for process to finish
					if (backgroundAllowed == 1 && bgCheck == 1)
					{
						//Save PID in case we need to kill it on exit
						for (i = 0; i < 250; i++)
						{
							if (kids[i] == -5)
							{
								kids[i] = childPID;
								break;
							}
						}

						//Return user to prompt while background process runs.
						waitpid(childPID, &childExitStatus, WNOHANG);
						printf("background pid is %d\n", childPID);
						fflush(stdout);						
					}

					//Foreground process. Must finish before continuing
					else
					{
						waitpid(childPID, &childExitStatus, 0);

						if (WIFEXITED(childExitStatus) != 0)
						{
							statusCode = WEXITSTATUS(childExitStatus);
						}

						else
						{
							statusCode = WTERMSIG(childExitStatus);
							printf("terminated by signal %d\n", statusCode);
							fflush(stdout);
						}						
					}					
				}				

				//Check to see if there are any background processes that have finished. If so, reap them.
				while ((childPID = waitpid(-1, &childExitStatus, WNOHANG)) > 0)
				{
					if (WIFEXITED(childExitStatus) != 0)
					{
						printf("background pid %d is done: exit value %d\n", childPID, WEXITSTATUS(childExitStatus));
						fflush(stdout);
					}

					else
					{
						printf("background pid %d is done: terminated by signal %d\n", childPID, WTERMSIG(childExitStatus));
						fflush(stdout);
					}

					//Remove finished ID from the list used in the exit command
					for (i = 0; i < 250; i++)
					{
						if (kids[i] == childPID)
						{
							kids[i] = -5;
							break;
						}
					}
				}

				//Unblock SIGTSTP for the parent so it can catch the signal.
				sigprocmask(SIG_UNBLOCK, &stopTSTP, NULL);
			}
		}
	}

    return 0;
}


