# *Small Shell*

**Small Shell** creates a shell.  User input is entered and parsed into commands. The shell recognizes three built-in commands: cd, status, and exit.  All other commands are forked and handled either in the foreground or background as designated. The user can change whether processes can run in the background. A list of active background processes is kept and all child processes are killed before the shell exits.

## Compile instructions

Make sure that smshell.c and the provided makefile are in the same folder. Compile the small shell program using the following command: make

To run the program after compilation, type: smshell
