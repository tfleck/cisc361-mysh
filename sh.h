
#include "get_path.h"

int pid;
int sh( int argc, char **argv, char **envp);
char *which(char *command, struct pathelement *pathlist );
int print_which(char *command, struct pathelement *pathlist);
int print_where(char *command, struct pathelement *pathlist);
void printenv(char **envp);

#define PROMPTMAX 32
#define MAXARGS 10
#define NUM_BUILTINS 11
