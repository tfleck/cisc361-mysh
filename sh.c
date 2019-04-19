#include "sh.h"

void child_handler(){
  //save errno from parent and reap zombie children
  int saved_errno = errno;
  while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
  errno = saved_errno;
}

int sh( int argc, char **argv, char **envp ){
  //Ignore CTRL-C and CTRL-Z
  signal(SIGINT, SIG_IGN); 
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  
  //Print exit values and ignore EOF (CTRL-D) by default
  setenv("PRINTEXITVALUE","1",0);
  setenv("IGNOREEOF","1",0);  
  
  //variable that determines if shell will keep looping
  int go = 1;

  //initialize shell status struct
  shState_t sh_state;

  //allocate 4 char* for pwd,owd,cwd,homedir
  sh_state.dirs = calloc(4, sizeof(char*));

  //check for and set timeout for fork() processes
  sh_state.timeout = -1;
  if(argc > 1){
    int temp_time = atoi(argv[1]);
    if(temp_time > 0){
      sh_state.timeout = temp_time;
    }
  }

  //allocate prompt
  sh_state.prompt = calloc(1,sizeof(char *));
  *(sh_state.prompt) = calloc(PROMPTMAX+1, sizeof(char));
  
  //get homedir
  struct passwd *password_entry;
  int uid = getuid();
  password_entry = getpwuid(uid);
  sh_state.dirs[3]= strdup(password_entry->pw_dir);

  //pwd - Printed Working Directory
  //cwd - Current Working Directory (full path)
  //owd - Old Working Directory (last full path)
  //get cwd
  if ( (sh_state.dirs[2] = getcwd(NULL, PATH_MAX+1)) == NULL )
  {
    perror("getcwd: ");
    exit(2);
  }

  //contract home path to ~ for display
  if(strncmp(sh_state.dirs[3],sh_state.dirs[2],strlen(sh_state.dirs[3])) == 0){
    sh_state.dirs[0] = calloc(strlen(sh_state.dirs[2])-2,sizeof(char));
    strncpy(sh_state.dirs[0],"~",1);
    strncpy(sh_state.dirs[0]+1,&(sh_state.dirs[2])[strlen(sh_state.dirs[3])],strlen(sh_state.dirs[2])-strlen(sh_state.dirs[3]));
  }
  else{
    sh_state.dirs[0] = calloc(strlen(sh_state.dirs[2]) + 1, sizeof(char));
    memcpy(sh_state.dirs[0], sh_state.dirs[2], strlen(sh_state.dirs[2]));
  }
  //owd = cwd to start
  sh_state.dirs[1] = calloc(strlen(sh_state.dirs[2]) + 1, sizeof(char));
  memcpy(sh_state.dirs[1], sh_state.dirs[2], strlen(sh_state.dirs[2]));
  
  // Put PATH into a linked list
  sh_state.pathlist = calloc(1,sizeof(struct pathelement *));
  *(sh_state.pathlist) = get_path();
  
  //default prompt prefix
  strncpy(*(sh_state.prompt)," ",1);
  
  while ( go )
  {
    //print prompt
    printf("%s[%s]> ",*(sh_state.prompt),sh_state.dirs[0]);

    // get command line and process
    char* temp_cmd = calloc(MAX_CANON, sizeof(char)); //always start with blank buffer
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
      //save file descriptors to be restored later
      sh_state.saved_stdin = dup(0);
      sh_state.saved_stdout = dup(1);
      sh_state.saved_stderr = dup(2);

      //save command input into shell state, minus carriage return
      sh_state.commandline = calloc(strlen(temp_cmd),sizeof(char));
      strncpy(sh_state.commandline,temp_cmd,strlen(temp_cmd)-1);

      //count vertical bar occurences for piping
      int bar_cnt = 0;
      unsigned int bar_ind;  
      for (bar_ind=0; bar_ind < strlen(sh_state.commandline); bar_ind++){
        if(strncmp(&(sh_state.commandline[bar_ind]),"|",1) == 0){
          bar_cnt++;
        }
      }
      bar_cnt++; //add 1 to get number of sh_state instances needed

      //split input on | and initialize sh_state for each
      shState_t all_states[bar_cnt]; 
      char *args_temp, *rest;
      const char s[2] = "|";
      int state_index = 0;
      args_temp = strtok_r(sh_state.commandline, s, &rest);
      while( args_temp != NULL ) {
        if(state_index < bar_cnt){
          all_states[state_index] = sh_state;
          //check for ampersand in argument
          if(strncmp(&(args_temp[0]),"& ",2) == 0){
            strncpy(args_temp,args_temp+2,strlen(args_temp)-2);
            strncpy(args_temp+strlen(args_temp)-2,"\0",1);
          }
          //check for ampersand in remaining string
          if(strncmp(&(rest[0]),"& ",2) == 0){
            //set stderr to stdout to go through pipe
            all_states[state_index].pipe_err = 1;
            all_states[state_index].commandline = args_temp;
          }
          else{
            //-1 will not be redirected
            all_states[state_index].pipe_err = -1;
            all_states[state_index].commandline = args_temp;
          }
          state_index++;
        }
        args_temp = strtok_r(NULL, s, &rest);
      }

      //run all sh_state(s) created above
      int status = EXIT_SUCCESS;
      if(bar_cnt <= 1){
        status = parse_run(&(all_states[0]),envp,-1,-1,-1);
      }
      else {
        int prev_pf = -1;
        //set child handler to reap background processes
        signal(SIGCHLD, child_handler);
        for(state_index=0; state_index<bar_cnt; state_index++){
          if(state_index < bar_cnt-1){ //not at end of list, keep piping
            int pfds[2];
            pipe(pfds);
            pid_t child_pid = fork();
            if(child_pid == 0){
              signal(SIGINT, SIG_DFL);
              signal(SIGTSTP, SIG_DFL);
              signal(SIGTERM, SIG_DFL);
              
              close(pfds[0]);
              parse_run(&(all_states[state_index]),envp,prev_pf,pfds[1],all_states[state_index].pipe_err);
              exit(0);
            }
            else{
              //this will be stdin of next executed
              prev_pf = pfds[0];
              close(pfds[1]);
            }
          }
          else{ //end of list
            status = parse_run(&(all_states[state_index]),envp,prev_pf,-1,-1);
          }
        }
      }

      //exit loop if failure/error
      if(status != EXIT_SUCCESS){
        go = 0;
      }

      //commandline is allocated every loop
      free(sh_state.commandline);

      //restore stdin, out & err
      close(0);
      dup(sh_state.saved_stdin);
      close(sh_state.saved_stdin);
      close(1);
      dup(sh_state.saved_stdout);
      close(sh_state.saved_stdout);
      close(2);
      dup(sh_state.saved_stderr);
      close(sh_state.saved_stderr);
    }
    //add return if EOF or other error
    else if(in_result == NULL){ 
      printf("\n");
    }
    //free command input memory
    free(temp_cmd);
  }
  //free all shared memory
  free(*(sh_state.prompt));
  free(sh_state.prompt);
  free(sh_state.dirs[0]);
  free(sh_state.dirs[1]);
  free(sh_state.dirs[2]);
  free(sh_state.dirs[3]);
  free(sh_state.dirs);
  free_path(&(*(sh_state.pathlist)));
  free(sh_state.pathlist);
  return 0;
} //sh()

int parse_run(shState_t *sh_state, char **envp, int set_stdin, int set_stdout, int set_stderr){
  //assign stdin, out, and err (-1 should be used for no change)
  if(set_stdin >= 0){
    close(0);
    dup(set_stdin);
    close(set_stdin);
  }
  if(set_stdout >= 0){
    close(1);
    dup(set_stdout);
    close(set_stdout);
  }
  if(set_stderr >= 0){
    close(2);
    dup(set_stderr);
    close(set_stderr);
  }

  //break up input command into individual arguments
  char *arg, *rest;
  const char s[2] = " ";
  sh_state->args = calloc(MAXARGS, sizeof(char*));
  arg = strtok_r(sh_state->commandline, s, &rest);
  sh_state->argsct = 0;
  sh_state->backgnd = 0;
  
  //loop over args and expand ~, and use glob to expand * and ?
  //memory for args is reallocated as needed to account for expansion
  while( arg != NULL ) {
    char* temp_arg = calloc(strlen(arg)+strlen(sh_state->dirs[3]),sizeof(char));
    if(strncmp("~",arg,1) == 0){
      strncpy(temp_arg,sh_state->dirs[3],strlen(sh_state->dirs[3]));
      strncpy(temp_arg+strlen(sh_state->dirs[3]),&arg[1],strlen(arg)-1);
      arg = temp_arg;
    }
    glob_t paths;
    int csource;
    char **p;
    csource = glob(arg, 0, NULL, &paths);
    if (csource == 0) { //can be expanded
      //add all expansions to args
      for (p = paths.gl_pathv; *p != NULL; ++p){
        if(sh_state->argsct >= MAXARGS){ //need to increase args memory allocation
          char** temp_args = (char **)realloc(sh_state->args,(sh_state->argsct+2)*sizeof(char*));
          if(temp_args != NULL){
            memset(temp_args+sh_state->argsct,0,2*sizeof(char*));
            sh_state->args = temp_args;
          }
          else{
            fprintf(stderr,"Error allocating memory\n");
            return 1;
          }
        }
        //duplicate so local copy can be freed
        sh_state->args[sh_state->argsct] = strdup(*p);
        sh_state->argsct++;
      }
      globfree(&paths);
    }
    else{ //no expansion
      if(sh_state->argsct >= MAXARGS){ //need to increase args memory allocation
        char** temp_args = (char **)realloc(sh_state->args,(sh_state->argsct+2)*sizeof(char*));
        if(temp_args != NULL){
          memset(temp_args+sh_state->argsct,0,2*sizeof(char*));
          sh_state->args = temp_args;
        }
        else{
          fprintf(stderr,"Error allocating memory\n");
          return 1;
        }
      }
      if(strcmp("&",arg) == 0){ //& will set stdin/stdout to /dev/null
        sh_state->backgnd = 1;
      }
      else{
        //duplicate so local copy can be freed
        sh_state->args[sh_state->argsct] = strdup(arg);
        sh_state->argsct++;
      }
    }
    arg = strtok_r(NULL, s, &rest);
    free(temp_arg);
  }

  int shortened = 0;
  if(sh_state->backgnd == 1){
    //Set stdin and stdout to /dev/null
    //stderr still prints to terminal
    int fd = open("/dev/null",O_WRONLY | O_CREAT | O_RDONLY);
    close(0);
    dup(fd);
    close(1);
    dup(fd);
    close(fd);
  }
  else{
    //check for i/o redirects
    int j;
    for(j = 1; j < sh_state->argsct-1; j++){
      if(strcmp(">",sh_state->args[j]) == 0){
        int fd = open(sh_state->args[j+1],O_RDWR | O_CREAT );
        close(1);
        dup(fd);
        close(fd);
        //shrink args by 2
        sh_state->argsct -= 2;
        memset(sh_state->args[j],0,strlen(sh_state->args[j]));
        memset(sh_state->args[j+1],0,strlen(sh_state->args[j+1]));
        shortened = 1;
        break;
      }
      else if(strcmp(">>",sh_state->args[j]) == 0){
        int fd = open(sh_state->args[j+1],O_RDWR | O_CREAT| O_APPEND );
        close(1);
        dup(fd);
        close(fd);
        //shrink args by 2
        sh_state->argsct -= 2;
        memset(sh_state->args[j],0,strlen(sh_state->args[j]));
        memset(sh_state->args[j+1],0,strlen(sh_state->args[j+1]));
        shortened = 1;
        break;
      }
      else if(strcmp(">&",sh_state->args[j]) == 0){
        int fd = open(sh_state->args[j+1],O_RDWR | O_CREAT );
        close(1);
        dup(fd);
        close(2);
        dup(fd);
        close(fd);
        //shrink args by 2
        sh_state->argsct -= 2;
        memset(sh_state->args[j],0,strlen(sh_state->args[j]));
        memset(sh_state->args[j+1],0,strlen(sh_state->args[j+1]));
        shortened = 1;
        break;
      }
      else if(strcmp(">>&",sh_state->args[j]) == 0){
        int fd = open(sh_state->args[j+1],O_RDWR | O_CREAT | O_APPEND );
        close(1);
        dup(fd);
        close(2);
        dup(fd);
        close(fd);
        //shrink args by 2
        sh_state->argsct -= 2;
        memset(sh_state->args[j],0,strlen(sh_state->args[j]));
        memset(sh_state->args[j+1],0,strlen(sh_state->args[j+1]));
        shortened = 1;
        break;
      }
      else if(strcmp("<",sh_state->args[j]) == 0){
        int fd = open(sh_state->args[j+1],O_RDONLY );
        close(0);
        dup(fd);
        close(fd);
        //shrink args by 2
        sh_state->argsct -= 2;
        memset(sh_state->args[j],0,strlen(sh_state->args[j]));
        memset(sh_state->args[j+1],0,strlen(sh_state->args[j+1]));
        shortened = 1;
        break;
      }
    }
  }
  
  int status = -1;
  if(strcmp("cd",sh_state->args[0]) == 0){
    status = builtin_cd(sh_state); 
  }//cd
  else if(strcmp("exit",sh_state->args[0]) == 0){
    status = builtin_exit(sh_state);
  }//exit
  else if(strcmp("kill",sh_state->args[0]) == 0){
    status = builtin_kill(sh_state);
  }//kill
  else if(strcmp("list",sh_state->args[0]) == 0){
    status = builtin_list(sh_state);
  }//list
  else if(strcmp("pid",sh_state->args[0]) == 0){
    status = builtin_pid(sh_state);
  }//pid
  else if(strcmp("printenv",sh_state->args[0]) == 0){
    status = builtin_printenv(sh_state, envp);
  }//printenv
  else if(strcmp("prompt",sh_state->args[0]) == 0){
    status = builtin_prompt(sh_state);
  }//prompt
  else if(strcmp("pwd",sh_state->args[0]) == 0){
    status = builtin_pwd(sh_state);
  }//pwd
  else if(strcmp("setenv",sh_state->args[0]) == 0){
    status = builtin_setenv(sh_state, envp);
  }//setenv
  else if(strcmp("where",sh_state->args[0]) == 0){
    status = builtin_where(sh_state);
  }//where
  else if(strcmp("which",sh_state->args[0]) == 0){
    status = builtin_which(sh_state);
  }//which
  else{
    status = execute_external(sh_state,envp);
  }
  //clean up duplicated memory
  int j;
  for(j = 0; j < sh_state->argsct; j++){
    free(sh_state->args[j]);
  }
  if(shortened){ //free memory from shrinking args
    free(sh_state->args[j]);
    free(sh_state->args[j+1]);
  }
  free(sh_state->args);
  return status;
} //parse_run()

char* which(char *command, struct pathelement *pathlist ) {
  //return first instance of command in pathlist
  while (pathlist != NULL) { // which
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
  while (pathlist != NULL) { // which
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

int builtin_cd(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: cd\n");
  if(sh_state->argsct > 2){
    fprintf(stderr,"cd only takes one argument, others will be ignored\n");
  }
  char* path;
  //cd to homedir if blank
  if(sh_state->argsct <= 1){
    path = sh_state->dirs[3];
  }
  else{
    path = sh_state->args[1];
  }
  char* temp_path = calloc(strlen(path)+strlen(sh_state->dirs[3]),sizeof(char));
  //cd to last directory if -
  if(strcmp("-",path) == 0){
    path = sh_state->dirs[1];
  }
  if(chdir(path) == 0){
    //set shared owd before updating vars
    free(sh_state->dirs[1]);
    sh_state->dirs[1] = calloc(strlen(sh_state->dirs[2]) + 1, sizeof(char));
    memcpy(sh_state->dirs[1], sh_state->dirs[2], strlen(sh_state->dirs[2]));
    //update shared cwd
    free(sh_state->dirs[2]);
    if ( (sh_state->dirs[2] = getcwd(NULL, PATH_MAX+1)) == NULL ){
      perror("getcwd: ");
      return EXIT_FAILURE;
    }
    free(sh_state->dirs[0]);
    //contract home path to ~ for display, and save full path
    if(strncmp(sh_state->dirs[3],sh_state->dirs[2],strlen(sh_state->dirs[3])) == 0){
      sh_state->dirs[0] = calloc(strlen(sh_state->dirs[2])-strlen(sh_state->dirs[3])+2,sizeof(char));
      strncpy(sh_state->dirs[0],"~",1);
      strncpy(sh_state->dirs[0]+1,&(sh_state->dirs[2])[strlen(sh_state->dirs[3])],strlen(sh_state->dirs[2])-strlen(sh_state->dirs[3]));
    }
    else{
      sh_state->dirs[3] = calloc(strlen(sh_state->dirs[2])+1,sizeof(char));
      memcpy(sh_state->dirs[0], sh_state->dirs[2], strlen(sh_state->dirs[2]));
    }
  }
  else{
    char msg_buffer[strlen(path)+11];
    sprintf(msg_buffer,"mysh: cd: %s",path);
    perror(msg_buffer);
  }
  free(temp_path);
  return EXIT_SUCCESS;
} //builtin_cd()

int builtin_exit(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: exit\n");
  return EXIT_FAILURE; //this will cause exit
} //builtin_exit()

int builtin_kill(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: kill\n");
  int signum = 15; //SIGTERM by default
  if(sh_state->argsct <= 1){
    fprintf(stderr,"Please provide a PID\n");
  }
  else{
    char* pidnum;
    //if -# flag, update signum
    if(strncmp("-",sh_state->args[1],1) == 0){
      char *c = calloc(strlen(sh_state->args[1]),sizeof(char));
      strncpy(c,sh_state->args[1]+1,strlen(sh_state->args[1])-1);
      signum = atoi(c);
      pidnum = sh_state->args[2];
      free(c);
    }
    else{
      pidnum = sh_state->args[1];
    }
    //convert input string to pid integer
    intmax_t xmax;
    char *tmp;
    pid_t x;
    errno = 0;
    xmax = strtoimax(pidnum, &tmp, 10);
    if(errno != 0 || tmp == pidnum || *tmp != '\0' || xmax != (pid_t)xmax){
      fprintf(stderr, "Bad PID!\n");
    } 
    else {
      //cast pid and execute signal
      x = (pid_t)xmax;
      kill(x,signum);
    }
  }
  return EXIT_SUCCESS;
} //builtin_kill()

int builtin_list(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: list\n");
   //list all items in current directory
  if(sh_state->argsct <= 1){
    DIR	*dp;
    struct dirent	*dirp;
    if ((dp = opendir(sh_state->dirs[2])) == NULL)
      fprintf(stderr,"can't open %s", sh_state->dirs[2]);
    while ((dirp = readdir(dp)) != NULL)
      printf("%s\n", dirp->d_name);
    closedir(dp);
  }
  else{
    //loop over all args, list all dirs specified
    int j;
    for(j = 1; j < sh_state->argsct; j++){
      printf("%s:\n",sh_state->args[j]);
      if(access(sh_state->args[j],R_OK) == 0){
        DIR	*dp;
        struct dirent	*dirp;
        if ((dp = opendir(sh_state->args[j])) == NULL)
          fprintf(stderr,"can't open %s\n", sh_state->args[j]);
        while ((dirp = readdir(dp)) != NULL)
          printf("  %s\n", dirp->d_name);
        closedir(dp);
      }
      else{
        perror("mysh: list");
      }
    }
  }
  return EXIT_SUCCESS;
} //builtin_list()

int builtin_pid(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: PID\n");
  printf("PID: %ld\n",(long)getpid());
  return EXIT_SUCCESS;
} //builtin_pid()

int builtin_printenv(shState_t *sh_state, char** envp){
  dprintf(sh_state->saved_stdout, "Executing built-in: printenv\n");
   //if no args, print all environment variables
  if(sh_state->argsct <= 1){
    char **env;
    for(env = envp; *env != 0; env++){
      char *thisEnv = *env;
      printf("%s\n", thisEnv);    
    }
  }
  else{
    //loop over all args and print environment variables if found
    int j;
    for(j = 1; j < sh_state->argsct; j++){
      char* temp_env = getenv(sh_state->args[j]);
      if(temp_env == NULL){
        fprintf(stderr, "Environment variable not found\n");
      }
      else{
        printf("%s\n",temp_env);
      }
    }
  }
  return EXIT_SUCCESS;
} //builtin_printenv()

int builtin_prompt(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: prompt\n");
  //if no args, have user enter new prompt prefix
  if(sh_state->argsct <= 1){
    free(*(sh_state->prompt));
    *(sh_state->prompt) = calloc(PROMPTMAX+1, sizeof(char));
    char* temp_prompt = calloc(PROMPTMAX, sizeof(char));
    printf("Input prompt prefix: ");
    fgets(temp_prompt,PROMPTMAX,stdin);
    strncpy(*(sh_state->prompt),temp_prompt,strlen(temp_prompt)-1);
    strncpy(*(sh_state->prompt)+strlen(*(sh_state->prompt))," ",1);
    free(temp_prompt);
  }
  else{
    //set arg to prompt prefix
    if(strlen(sh_state->args[1]) > 0 && strlen(sh_state->args[1]) <= PROMPTMAX){
      free(*(sh_state->prompt));
      *(sh_state->prompt) = calloc(PROMPTMAX+1, sizeof(char));
      strncpy(*(sh_state->prompt),sh_state->args[1],strlen(sh_state->args[1]));
      strncpy(*(sh_state->prompt)+strlen(sh_state->args[1])," ",1);
    }
  }
  return EXIT_SUCCESS;
} //builtin_printenv()

int builtin_pwd(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: pwd\n");
  //update shared cwd and print
  free(sh_state->dirs[2]);
  if ( (sh_state->dirs[2] = getcwd(NULL, PATH_MAX+1)) == NULL ){
    perror("getcwd: ");
    return EXIT_FAILURE;
  }
  printf("%s\n",sh_state->dirs[2]);
  return EXIT_SUCCESS;
} //builtin_pwd()

int builtin_setenv(shState_t *sh_state, char **envp){
  dprintf(sh_state->saved_stdout, "Executing built-in: setenv\n");
  //if no args, print all environment variables
  if(sh_state->argsct <= 1){
    char **env;
    for(env = envp; *env != 0; env++){
      char *thisEnv = *env;
      printf("%s\n", thisEnv);    
    }
  }
  else{
    //if no argument for value, set env var to blank
    if(sh_state->argsct == 2){
      setenv(sh_state->args[1],"",1);
    }
    else{
      if(sh_state->argsct > 3){
        fprintf(stderr,"setenv only takes 2 arguments, others will be ignored\n");
      }
      char* path = sh_state->args[2];
      setenv(sh_state->args[1],path,1);
      //update shared PATH list
      if(strcmp("PATH",sh_state->args[1]) == 0){
        free_path(&(*(sh_state->pathlist)));
        *(sh_state->pathlist) = get_path();
      }
      //update shared homedir and pwd
      else if(strcmp("HOME",sh_state->args[1]) == 0){
        sh_state->dirs[3] = getenv("HOME");
        free(sh_state->dirs[0]);
        if(strncmp(sh_state->dirs[3],sh_state->dirs[2],strlen(sh_state->dirs[3])) == 0){
          sh_state->dirs[0] = calloc(strlen(sh_state->dirs[2])-strlen(sh_state->dirs[3])+2,sizeof(char));
          strncpy(sh_state->dirs[0],"~",1);
          strncpy(sh_state->dirs[0]+1,&(sh_state->dirs[2])[strlen(sh_state->dirs[3])],strlen(sh_state->dirs[2])-strlen(sh_state->dirs[3]));
        }
      }
    }
  }
  return EXIT_SUCCESS;
} //builtin_setenv()

int builtin_where(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: where\n");
  if(sh_state->argsct <= 1){
    fprintf(stderr,"Please provide an executable name\n");
  }
  else{
    int j;
    for(j = 1; j < sh_state->argsct; j++){
      printf("%s:\n",sh_state->args[j]);
      //print for builtins
      if(!strcmp("cd",sh_state->args[j])){
        printf("Shell built-in command\n");
      }
      else if(!strcmp("exit",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("kill",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("list",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("pid",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("printenv",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("prompt",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("pwd",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("setenv",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("where",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("which",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      //print all instances of executable in PATH
      else{
        int status = print_where(sh_state->args[j],*(sh_state->pathlist));
        if(status == 0){
          fprintf(stderr,"Executable not found\n");
        }
      }
    }
  }
  return EXIT_SUCCESS;
} //builtin_where()

int builtin_which(shState_t *sh_state){
  dprintf(sh_state->saved_stdout, "Executing built-in: which\n");
  if(sh_state->argsct <= 1){
    fprintf(stderr,"Please provide an executable name\n");
  }
  else{
    int j;
    for(j = 1; j < sh_state->argsct; j++){
      printf("%s:\n",sh_state->args[j]);
      //print for builtins
      if(!strcmp("cd",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("exit",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("kill",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("list",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("pid",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("printenv",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("prompt",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("pwd",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("setenv",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("where",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      else if(!strcmp("which",sh_state->args[j])){
        printf("  Shell built-in command\n");
      }
      //print first instance of executable in PATH
      else{
        int status = print_which(sh_state->args[j],*(sh_state->pathlist));
        if(status == 0){
          fprintf(stderr,"Executable not found\n");
        }
      }
    }
  }
  return EXIT_SUCCESS;
} //builtin_which()

int execute_external(shState_t *sh_state, char **envp){
  if(sh_state->argsct > 0){
    char *path = NULL;
    //if path, check if executable and not directory
    if(access(sh_state->args[0], X_OK) == 0){
      struct stat statbuf;
      if (stat(sh_state->args[0], &statbuf) == 0){
        if(S_ISDIR(statbuf.st_mode) == 0){
          path = sh_state->args[0];
        }
      }
    }
    else{
      path = which(sh_state->args[0],*(sh_state->pathlist));
    }
    if(path != NULL){
      dprintf(sh_state->saved_stdout, "Executing: %s\n",path);
      if(sh_state->backgnd == 1){
        //Catch SIGCHLD and reap for backgrounded processes
        signal(SIGCHLD, child_handler);
      }
      else{
        signal(SIGCHLD, SIG_DFL);
      }
      pid_t child_pid = fork();
      if(child_pid < 0){
        fprintf(stderr,"Error creating child process\n");
      }
      else if(child_pid == 0){
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        execve(path,sh_state->args, envp);
      }
      else if(child_pid > 0 && sh_state->backgnd == 0){
        int child_status;
        //Check if timeout has passed, if it is set
        clock_t start_time = clock();
        while(waitpid(child_pid,&child_status,WNOHANG) == 0){
          float diff_t = ((float)(clock() - start_time) / (float)CLOCKS_PER_SEC);
          if(sh_state->timeout > 0 && diff_t >= sh_state->timeout){
            kill(child_pid,SIGINT); //Send CTRL-C to child process, sending SIGKILL caused problems with prompt line, even after exiting mysh
            waitpid(child_pid,&child_status,0);
            printf("!!! taking too long to execute this command !!!\n");
          }
        }
        //If shell var PRINTEXITVALUE is set, print child exit status
        char* print_status = getenv("PRINTEXITVALUE");
        if(print_status != NULL && strcmp(print_status,"1") == 0 && WIFEXITED(child_status)){
          if(WEXITSTATUS(child_status) != 0){
            fprintf(stderr, "Exit status of child was: %d\n",WEXITSTATUS(child_status));
          }
        }
      }
    }
    else{
      fprintf(stderr, "%s: Command not found.\n", sh_state->args[0]);
    }
    //free path if allocated by which
    if(path != NULL && strcmp(sh_state->args[0],path) != 0){
      free(path);
    }
  }
  return EXIT_SUCCESS;
} //execute_external()