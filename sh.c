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
#include "sh.h"

int sh( int argc, char **argv, char **envp )
{
  //Ignore CTRL-C and CTRL-Z
  signal(SIGINT, SIG_IGN); 
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  
  //Print exit values and ignore EOF (CTRL-D) by default
  setenv("PRINTEXITVALUE","1",0);
  setenv("IGNOREEOF","1",0);
  
  //check for and set timeout for fork() processes
  int timeout = -1;
  if(argc > 1){
    int temp_time = atoi(argv[1]);
    if(temp_time > 0){
      timeout = temp_time;
    }
  }
  
  char *prompt = calloc(PROMPTMAX+1, sizeof(char));
  char *arg, *temp_cmd, *commandline, *rest, *pwd, *owd, *cwd;
  char **args;
  const char s[2] = " ";
  int uid, j, argsct, go = 1;
  struct passwd *password_entry;
  char *homedir;
  struct pathelement *pathlist;
  
  uid = getuid();
  password_entry = getpwuid(uid);               /* get passwd info */
  homedir = password_entry->pw_dir;		/* Home directory to start out with*/
     
  if ( (cwd = getcwd(NULL, PATH_MAX+1)) == NULL )
  {
    perror("getcwd: ");
    exit(2);
  }
  //pwd - Printed Working Directory
  //cwd - Current Working Directory (full path)
  //owd - Old Working Directory (last full path)
  //contract home path to ~ for display
  if(strncmp(homedir,cwd,strlen(homedir)) == 0){
    pwd = calloc(strlen(cwd)-2,sizeof(char));
    strncpy(pwd,"~",1);
    strncpy(pwd+1,&cwd[strlen(homedir)],strlen(cwd)-strlen(homedir));
  }
  else{
    pwd = calloc(strlen(cwd) + 1, sizeof(char));
    memcpy(pwd, cwd, strlen(cwd));
  }
  //owd = cwd to start
  owd = calloc(strlen(cwd) + 1, sizeof(char));
  memcpy(owd, cwd, strlen(cwd));

  /* Put PATH into a linked list */
  pathlist = get_path();
  
  //default prompt prefix
  strncpy(prompt," ",1);
  
  while ( go )
  {
    /* print your prompt */
    printf("%s[%s]> ",prompt,pwd);

    /* get command line and process */
    temp_cmd = calloc(MAX_CANON, sizeof(char)); //always start with blank buffer
    char* in_result = fgets(temp_cmd,MAX_CANON,stdin);
    
    //Check if EOF (CTRL-D) should be ignored or not
    int checked = 0;
    char* eof_ignore = getenv("IGNOREEOF");
    if(eof_ignore != NULL && strcmp(eof_ignore,"1") != 0){
      checked = 1;
    }
    else if(temp_cmd[0] != '\0' && strlen(temp_cmd) > 1 && in_result != NULL){
      checked = 1;
    }
    
    //check if input is ok to continue
    if(temp_cmd[0] != '\n' && checked == 1){
      args = calloc(MAXARGS, sizeof(char*));
      commandline = calloc(strlen(temp_cmd),sizeof(char));
      strncpy(commandline,temp_cmd,strlen(temp_cmd)-1);
      arg = strtok_r(commandline, s, &rest);
      argsct = 0;
      
      //loop over args and expand ~, and use glob to expand * and ?
      //memory for args is reallocated as needed to account for expansion
      while( arg != NULL ) {
        char* temp_arg = calloc(strlen(arg)+strlen(homedir),sizeof(char));
        if(strncmp("~",arg,1) == 0){
          strncpy(temp_arg,homedir,strlen(homedir));
          strncpy(temp_arg+strlen(homedir),&arg[1],strlen(arg)-1);
          arg = temp_arg;
        }
        glob_t paths;
        int csource;
        char **p;
        csource = glob(arg, 0, NULL, &paths);
        if (csource == 0) {
          for (p = paths.gl_pathv; *p != NULL; ++p){
            if(argsct >= MAXARGS-1){
              char **temp_args = args;
              args = calloc(argsct+2,sizeof(char*));
              for(j = 0; j < argsct; j++){
                args[j] = temp_args[j];
              }
              free(temp_args);
              if(args == NULL){
                fprintf(stderr,"Error allocating memory\n");
                return 1;
              }
            }
            args[argsct] = strdup(*p);
            argsct++;
          }
          globfree(&paths);
        }
        else{
          if(argsct >= MAXARGS-1){
            char **temp_args = args;
            args = calloc(argsct+2,sizeof(char*));
            for(j = 0; j < argsct; j++){
              args[j] = temp_args[j];
            }
            free(temp_args);
            if(args == NULL){
              fprintf(stderr,"Error allocating memory\n");
              return 1;
            }
          }
          args[argsct] = strdup(arg);
          argsct++;
        }
        arg = strtok_r(NULL, s, &rest);
        free(temp_arg);
      }
  
      /* check for each built in command and implement */
      
      //cd
      if(strcmp("cd",args[0]) == 0){
        printf("Executing built-in: cd\n");
        if(argsct > 2){
          fprintf(stderr,"cd only takes one argument, others will be ignored\n");
        }
        char* path;
        //cd to home if blank
        if(argsct <= 1){
          path = homedir;
        }
        else{
          path = args[1];
        }
        char* temp_path = calloc(strlen(path)+strlen(homedir),sizeof(char));
        //cd to last directory
        if(strcmp("-",path) == 0){
          path = owd;
        }
        if(chdir(path) == 0){
          //set owd before updating vars
          free(owd);
          owd = calloc(strlen(cwd) + 1, sizeof(char));
          memcpy(owd, cwd, strlen(cwd));
          //update cwd
          free(cwd);
          if ( (cwd = getcwd(NULL, PATH_MAX+1)) == NULL ){
            perror("getcwd: ");
            go = 0;
          }
          free(pwd);
          //contract home path to ~ for display, keep 
          if(strncmp(homedir,cwd,strlen(homedir)) == 0){
            pwd = calloc(strlen(cwd)-strlen(homedir)+2,sizeof(char));
            strncpy(pwd,"~",1);
            strncpy(pwd+1,&cwd[strlen(homedir)],strlen(cwd)-strlen(homedir));
          }
          else{
            pwd = calloc(strlen(cwd)+1,sizeof(char));
            memcpy(pwd, cwd, strlen(cwd));
          }
        }
        else{
          char msg_buffer[strlen(path)+11];
          sprintf(msg_buffer,"mysh: cd: %s",path);
          perror(msg_buffer);
        }
        free(temp_path);
      }//cd
      //exit
      else if(strcmp("exit",args[0]) == 0){
        printf("Executing built-in: exit\n");
        go = 0;
      }//exit
      //kill
      else if(strcmp("kill",args[0]) == 0){
        printf("Executing built-in: kill\n");
        int signum = 15; //SIGTERM by default
        if(argsct <= 1){
          fprintf(stderr,"Please provide a PID\n");
        }
        else{
          char* pidnum;
          //if -# flag, update signum
          if(strncmp("-",args[1],1) == 0){
            char *c = calloc(strlen(args[1]),sizeof(char));
            strncpy(c,args[1]+1,strlen(args[1])-1);
            signum = atoi(c);
            pidnum = args[2];
            free(c);
          }
          else{
            pidnum = args[1];
          }
          intmax_t xmax;
          char *tmp;
          pid_t x;
          errno = 0;
          xmax = strtoimax(pidnum, &tmp, 10);
          if(errno != 0 || tmp == pidnum || *tmp != '\0' || xmax != (pid_t)xmax){
            fprintf(stderr, "Bad PID!\n");
          } else {
            x = (pid_t)xmax;
            kill(x,signum);
          }
        }
      }//kill
      //list
      else if(strcmp("list",args[0]) == 0){
        printf("Executing built-in: list\n");
        //list current directory
        if(argsct <= 1){
          DIR	*dp;
    	    struct dirent	*dirp;
          if ((dp = opendir(cwd)) == NULL)
    		    fprintf(stderr,"can't open %s", cwd);
    	    while ((dirp = readdir(dp)) != NULL)
    		    printf("%s\n", dirp->d_name);
    		  closedir(dp);
        }
        else{
          //loop over all args, list all dirs specified
          for(j = 1; j < argsct; j++){
            printf("%s:\n",args[j]);
            if(access(args[j],R_OK) == 0){
              DIR	*dp;
        	    struct dirent	*dirp;
              if ((dp = opendir(args[j])) == NULL)
        		    fprintf(stderr,"can't open %s\n", args[j]);
        	    while ((dirp = readdir(dp)) != NULL)
        		    printf("  %s\n", dirp->d_name);
        		  closedir(dp);
            }
            else{
              perror("mysh: list");
            }
          }
        }
      }//list
      //pid
      else if(strcmp("pid",args[0]) == 0){
        printf("Executing built-in: PID\n");
        printf("PID: %ld\n",(long)getpid());
      }//pid
      //printenv
      else if(strcmp("printenv",args[0]) == 0){
        printf("Executing built-in: printenv\n");
        //if no args, print all environment variables
        if(argsct <= 1){
          char **env;
          for(env = envp; *env != 0; env++){
            char *thisEnv = *env;
            printf("%s\n", thisEnv);    
          }
        }
        else{
          //loop over all args and print environment variables if found
          for(j = 1; j < argsct; j++){
            char* temp_env = getenv(args[j]);
            if(temp_env == NULL){
              fprintf(stderr, "Environment variable not found\n");
            }
            else{
              printf("%s\n",temp_env);
            }
          }
        }
      }//printenv
      //prompt
      else if(strcmp("prompt",args[0]) == 0){
        printf("Executing built-in: prompt\n");
        //if no args, have user enter new prompt prefix
        if(argsct <= 1){
          free(prompt);
          prompt = calloc(PROMPTMAX+1, sizeof(char));
          char* temp_prompt = calloc(PROMPTMAX, sizeof(char));
          printf("Input prompt prefix: ");
          fgets(temp_prompt,PROMPTMAX,stdin);
          strncpy(prompt,temp_prompt,strlen(temp_prompt)-1);
          strncpy(prompt+strlen(prompt)," ",1);
          free(temp_prompt);
        }
        else{
          //set arg to prompt prefix
          if(strlen(args[1]) > 0 && strlen(args[1]) <= PROMPTMAX){
            free(prompt);
            prompt = calloc(PROMPTMAX+1, sizeof(char));
            strncpy(prompt,args[1],strlen(args[1]));
            strncpy(prompt+strlen(args[1])," ",1);
          }
        }
      }//prompt
      //pwd
      else if(strcmp("pwd",args[0]) == 0){
        printf("Executing built-in: pwd\n");
        //update cwd and print
        free(cwd);
        if ( (cwd = getcwd(NULL, PATH_MAX+1)) == NULL ){
          perror("getcwd: ");
          go = 0;
        }
        printf("%s\n",cwd);
      }//pwd
      //setenv
      else if(strcmp("setenv",args[0]) == 0){
        printf("Executing built-in: setenv\n");
        //if no args, print all environment variables
        if(argsct <= 1){
          char **env;
          for(env = envp; *env != 0; env++){
            char *thisEnv = *env;
            printf("%s\n", thisEnv);    
          }
        }
        else{
          //if no argument for value, set env var to blank
          if(argsct == 2){
            setenv(args[1],"",1);
          }
          else{
            char* path = args[2];
            if(argsct > 3){
              fprintf(stderr,"setenv only takes 2 arguments, others will be ignored\n");
            }
            setenv(args[1],path,1);
            //update internal PATH list
            if(strcmp("PATH",args[1]) == 0){
              free_path(&pathlist);
              pathlist = get_path();
            }
            //update homedir variable and pwd
            else if(strcmp("HOME",args[1]) == 0){
              homedir = getenv("HOME");
              free(pwd);
              if(strncmp(homedir,cwd,strlen(homedir)) == 0){
                pwd = calloc(strlen(cwd)-strlen(homedir)+2,sizeof(char));
                strncpy(pwd,"~",1);
                strncpy(pwd+1,&cwd[strlen(homedir)],strlen(cwd)-strlen(homedir));
              }
            }
          }
        }
      }//setenv
      //where
      else if(strcmp("where",args[0]) == 0){
        printf("Executing built-in: where\n");
        if(argsct <= 1){
          fprintf(stderr,"Please provide an executable name\n");
        }
        else{
          //loop over args and print command location
          for(j = 1; j < argsct; j++){
            printf("%s:\n",args[j]);
            //print for builtins
            if(!strcmp("cd",args[j])){
              printf("Shell built-in command\n");
            }
            else if(!strcmp("exit",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("kill",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("list",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("pid",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("printenv",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("prompt",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("pwd",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("setenv",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("where",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("which",args[j])){
              printf("  Shell built-in command\n");
            }
            //print all instances of executable in PATH
            else{
              int status = print_where(args[j],pathlist);
              if(status == 0){
                fprintf(stderr,"Executable not found\n");
              }
            }
          }
        }
      }//where
      //which
      else if(strcmp("which",args[0]) == 0){
        printf("Executing built-in: which\n");
        if(argsct <= 1){
          fprintf(stderr,"Please provide an executable name\n");
        }
        else{
          for(j = 1; j < argsct; j++){
            printf("%s:\n",args[j]);
            //print for builtins
            if(!strcmp("cd",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("exit",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("kill",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("list",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("pid",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("printenv",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("prompt",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("pwd",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("setenv",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("where",args[j])){
              printf("  Shell built-in command\n");
            }
            else if(!strcmp("which",args[j])){
              printf("  Shell built-in command\n");
            }
            //print first instance of executable in PATH
            else{
              int status = print_which(args[j],pathlist);
              if(status == 0){
                fprintf(stderr,"Executable not found\n");
              }
            }
          }
        }
      }//which
       /*  else  program to exec */
      else {
         /* find it */
         /* do fork(), execve() and waitpid() */
        if(argsct > 0){
          char *path = NULL;
          //if path, check if executable and not directory
          if(access(args[0], X_OK) == 0){
            struct stat statbuf;
            if (stat(args[0], &statbuf) == 0){
              if(S_ISDIR(statbuf.st_mode) == 0){
                path = args[0];
              }
            }
          }
          else{
            path = which(args[0],pathlist);
          }
          if(path != NULL){
            printf("Executing: %s\n",path);
            pid_t child_pid = fork();
            if(child_pid < 0){
              fprintf(stderr,"Error creating child process\n");
            }
            else if(child_pid == 0){
              execve(path,args,envp);
            }
            else if(child_pid > 0){
              int child_status;
              //Check if timeout has passed, if it is set
              clock_t start_time = clock();
              while(waitpid(child_pid,&child_status,WNOHANG) == 0){
                float diff_t = ((float)(clock() - start_time) / (float)CLOCKS_PER_SEC);
                if(timeout > 0 && diff_t >= timeout){
                  kill(child_pid,SIGINT); //Send CTRL-C to child process, sending SIGKILL caused problems with prompt line, even after exiting mysh
                  waitpid(child_pid,&child_status,0);
                  printf("!!! taking too long to execute this command !!!\n");
                }
              }
              //If shell var PRINTEXITVALUE is set, print child exit status
              char* print_status = getenv("PRINTEXITVALUE");
              if(print_status != NULL && strcmp(print_status,"1") == 0){
                if(WEXITSTATUS(child_status) != 0){
                  fprintf(stderr, "Exit status of child was: %d\n",WEXITSTATUS(child_status));
                }
              }
            }
          }
          else{
            fprintf(stderr, "%s: Command not found.\n", args[0]);
          }
          //free path if allocated by which
          if(path != NULL && strcmp(args[0],path) != 0){
            free(path);
          }
        }
      }
      //free command buffers to be reallocated every loop
      free(temp_cmd);
      free(commandline);
      for(j = 0; j < argsct; j++){
        free(args[j]);
      }
      free(args);
    }
    //add return if EOF or other error
    else if(in_result == NULL){ 
      printf("\n");
    }
  }
  free(prompt);
  free(arg);
  free(cwd);
  free(pwd);
  free(owd);
  free_path(&pathlist);
  return 0;
} /* sh() */

char *which(char *command, struct pathelement *pathlist ) {
  //return first instance of command in pathlist
  while (pathlist != NULL) {         // which
    char* cmd = calloc(PATH_MAX,sizeof(char));
    sprintf(cmd, "%s/%s", pathlist->element,command);
    if (access(cmd, X_OK) == 0) {
      return cmd;
    }
    pathlist = pathlist->next;
    free(cmd);
  }
  return NULL;
} //which()

int print_which(char *command, struct pathelement *pathlist ) {
  //print first instance of command in pathlist, and return # found
  int status = 0;
  char* cmd = which(command,pathlist);
  if(cmd != NULL){
    printf("  %s\n",cmd);
    free(cmd);
    status++;
    return status;
  }
  return status;
} //print_which()

int print_where(char *command, struct pathelement *pathlist ) {
  //print all instances of command in pathlist, and return # found
  int status = 0;
  while (pathlist != NULL) {         // which
    char* cmd = calloc(PATH_MAX,sizeof(char));
    sprintf(cmd, "%s/%s", pathlist->element,command);
    if (access(cmd, X_OK) == 0) {
      printf("  %s\n",cmd);
      status++;
    }
    pathlist = pathlist->next;
    free(cmd);
  }
  return status;
} //print_where()