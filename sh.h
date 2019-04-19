#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <inttypes.h>
#include <errno.h>
#include <glob.h>
#include <time.h> 
#include <fcntl.h>
#include <pthread.h> 
#include <stdlib.h>
#include "get_path.h"

typedef struct shState_t
{
   //unique vars
   char *commandline;
   char **args;
   int argsct, backgnd, timeout, saved_stdin, saved_stdout, saved_stderr, pipe_err;

   //shared vars
   //dirs[0]=pwd, dirs[1]=owd, dirs[2]=cwd, dirs[3]=homedir
   char **dirs, **prompt;
   struct pathelement **pathlist;
} shState_t ;

int pid;
int sh( int argc, char **argv, char **envp);
int parse_run(shState_t *sh_state, char **envp, int set_stdin, int set_stdout, int set_stderr);
char* which(char *command, struct pathelement *pathlist );
int print_which(char *command, struct pathelement *pathlist);
int print_where(char *command, struct pathelement *pathlist);
int builtin_cd(shState_t *sh_state);
int builtin_exit(shState_t *sh_state);
int builtin_kill(shState_t *sh_state);
int builtin_list(shState_t *sh_state);
int builtin_pid(shState_t *sh_state);
int builtin_printenv(shState_t *sh_state, char** envp);
int builtin_prompt(shState_t *sh_state);
int builtin_pwd(shState_t *sh_state);
int builtin_setenv(shState_t *sh_state, char **envp);
int builtin_where(shState_t *sh_state);
int builtin_which(shState_t *sh_state);
int execute_external(shState_t *sh_state, char **envp);

//Maximum prompt prefix length
#define PROMPTMAX 32
//Maximum shell arguments
#define MAXARGS 10
