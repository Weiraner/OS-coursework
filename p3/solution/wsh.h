#ifndef WSH_H
#define WSH_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>

extern int status;

typedef struct ShellVar
{
    char *name;
    char *value;
    struct ShellVar *next;
} ShellVar;

void myExit(int status);
void cd(char *arguments[]);
void myExport(char *arguments[]);
void vars(ShellVar *head);
// void history();
// void ls();
void setShellVar(ShellVar **head, char *name, char *value);
void local(char **arguments, ShellVar **head);
int parseFd(char *argument, char *token_loc);
int handleRedirection(char *argument);
int handleBuiltinCase(char **arg_arr, ShellVar **head, int arg_num);
char **parseArguments(char *command, int *num, int read);
void handleVariables(char **arguments, ShellVar *head, int arg_num);
int seperateArgument(char **before, char **after, char *argument, char *token);

#endif // WSH_H
