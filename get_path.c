/*
  get_path.c
  Ben Miller

  Just a little sample function that gets the PATH env var, parses it and
  puts "components" into a linked list, which is returned.
*/
#include "get_path.h"

struct pathelement *get_path()
{
  /* path is a copy of the PATH and p is a temp pointer */
  char *path, *p;

  /* tmp is a temp point used to create a linked list and pathlist is a
     pointer to the head of the list */
  struct pathelement *tmp, *pathlist = NULL;

  p = getenv("PATH");	/* get a pointer to the PATH env var.
			   make a copy of it, since strtok modifies the
			   string that it is working with... */
  path = malloc((strlen(p)+1)*sizeof(char));	/* use malloc(3) */
  strncpy(path, p, strlen(p));
  path[strlen(p)] = '\0';
  char *rest;
  
  //use strtok_r and strdup instead to allow for better memory management
  p = strtok_r(path, ":", &rest); 	/* PATH is : delimited */
  while( p != NULL )				/* loop through the PATH */
  {				/* to build a linked list of dirs */
    if ( !pathlist )		/* create head of list */
    {
      tmp = calloc(1, sizeof(struct pathelement));
      pathlist = tmp;
    }
    else			/* add on next element */
    {
      tmp->next = calloc(1, sizeof(struct pathelement));
      tmp = tmp->next;
    }
    tmp->element = strdup(p);	
    tmp->next = NULL;
    p = strtok_r(NULL, ":", &rest);
  }
  free(path);
  return pathlist;
} /* end get_path() */

//loop over path and free all memory
void free_path(struct pathelement **path){
  struct pathelement *c = *path;
  struct pathelement *n;
  while(c != NULL){
    n = c->next;
    if(c->element != NULL){
      free(c->element);
    }
    free(c);
    c = n;
  }
  c = NULL;
} //free_path()
