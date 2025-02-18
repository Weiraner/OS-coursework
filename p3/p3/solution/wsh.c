#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>

// record the return value of latest command
int status = 0;
// record the length of current history record
int historyLen = 5;
char **historyRecord = NULL;
// track if it is a valid "history <n>"
int isRepeated;

// define shell variable structure
typedef struct ShellVar
{
    char *name;
    char *value;
    struct ShellVar *next;
} ShellVar;

// helper method to cut string before identifier and after it
int seperateArgument(char **before, char **after, char *argument, char *token)
{
    char *loc = strstr(argument, token);
    // token doesn't exist in the argument
    if (loc == NULL)
    {
        return -1;
    }

    // if exist
    // before token
    *before = (char *)malloc(sizeof(char) * (loc - argument + 1));
    strncpy(*before, argument, loc - argument);
    (*before)[loc - argument] = '\0';

    // after token
    *after = (char *)malloc(sizeof(char) * (argument + strlen(argument) - loc));
    strncpy(*after, loc + 1, argument + strlen(argument) - loc - 1);
    (*after)[argument + strlen(argument) - loc - 1] = '\0';

    return 0;
}

void cd(char *arguments[])
{

    // error when chdir fails
    if (chdir(arguments[1]) != 0)
    {
        perror("chdir error\n");
        status = -1;
        return;
    }

    // on success
    status = 0;
    return;
}
void myExport(char *arguments[])
{
    // parse var name and var value
    char *name = NULL;
    char *value = NULL;
    if (seperateArgument(&name, &value, arguments[1], "=") == -1)
    {
        perror("local wrong format\n");
        status = -1;
        return;
    }

    // set new environment var
    if (setenv(name, value, 1) != 0)
    {
        perror("setenv fault\n");
        status = -1;
        free(name);
        free(value);
        return;
    }

    // on success
    status = 0;
    free(name);
    free(value);
}

void vars(ShellVar *head)
{
    ShellVar *cur = head;
    while (cur != NULL)
    {
        printf("%s=%s\n", cur->name, cur->value);
        cur = cur->next;
    }
    // on success
    status = 0;
    return;
}
void history(char *arguments[], int num)
{
    // history
    if (num == 1)
    {
        for (int i = 0; i < historyLen; i++)
        {
            if (historyRecord[i] == NULL)
                break;
            printf("%d) %s\n", i + 1, historyRecord[i]);
        }
    }
    // history n
    else if (num == 2)
    {
        // if the format is wrong
        if (atoi(arguments[1]) <= 0 || atoi(arguments[1]) > historyLen)
        {
            perror("error index of history array\n");
            status = -1;
        }
        else
        {
            // if record at index n is NULL
            if (historyRecord[atoi(arguments[1]) - 1] == NULL)
            {
                perror("record at index n is NULL\n");
                status = -1;
            }
            // correctly call nth command in history
            else
            {
                isRepeated = atoi(arguments[1]);
            }
        }
    }
    // history set n
    else if (num == 3)
    {
        // if the format is wrong
        if (strcmp(arguments[1], "set") != 0 || atoi(arguments[2]) <= 0)
        {
            perror("error length to reset history\n");
            status = -1;
        }
        // no need to reset if the length still the same
        else if (atoi(arguments[2]) != historyLen)
        {
            int newLen = atoi(arguments[2]);
            char **newHisRecord = malloc(sizeof(char *) * newLen);

            // copy every record in old history into new one
            for (int i = 0; i < newLen; i++)
            {
                if (i >= historyLen || historyRecord[i] == NULL)
                {
                    newHisRecord[i] = NULL;
                }
                else
                {
                    newHisRecord[i] = strdup(historyRecord[i]);
                }
            }

            // free old history record
            for (int i = 0; i < historyLen; i++)
            {
                free(historyRecord[i]);
            }
            free(historyRecord);

            // set new history record
            historyLen = newLen;
            historyRecord = newHisRecord;
        }
    }
    // else are all invalid
    else
    {
        status = -1;
    }
    return;
}
void ls()
{
    struct dirent **namelist;
    int i, n;

    n = scandir(".", &namelist, 0, alphasort);
    if (n < 0)
    {
        perror("scandir error\n");
        status = -1;
        return;
    }
    else
    {
        for (i = 0; i < n; i++)
        {
            if (namelist[i]->d_name[0] != '.')
            {
                printf("%s\n", namelist[i]->d_name);
            }
            free(namelist[i]);
        }
    }
    free(namelist);
}

// Shell variable helper
void setShellVar(ShellVar **head, char *name, char *value)
{
    // create a new shell variable
    ShellVar *new = malloc(sizeof(ShellVar));
    new->name = name;
    new->value = value;
    new->next = NULL;

    // if head is NULL
    if (*head == NULL)
    {
        *head = new;
        return;
    }
    ShellVar *cur = *head;
    ShellVar *prev = NULL;
    while (cur != NULL)
    {
        // if variable exists
        if (strcmp(cur->name, name) == 0)
        {

            free(cur->value);
            free(new->name);
            //? not free new->value
            free(new);
            cur->value = value;
            if (cur->value == NULL)
            {
                perror("memory allocation fail");
                status = -1;
                return;
            }
            return;
        }
        // iterate to next one
        prev = cur;
        cur = cur->next;
    }

    prev->next = new;
    return;
}

void local(char **arguments, ShellVar **head)
{
    // parse var name and var value
    char *name = NULL;
    char *value = NULL;
    if (seperateArgument(&name, &value, arguments[1], "=") == -1)
    {
        perror("local wrong format\n");
        status = -1;
        return;
    }

    // set new shell var
    setShellVar(head, name, value);

    // on success
    status = 0;
    return;
}

// parse file descriptor n in command
int parseFd(char *argument, char *token_loc)
{
    // count the length of fd
    int len = token_loc - argument;

    // if there is no fd, return -1;
    if (len == 0)
        return -1;

    // convert file descriptor from char to int
    char fd_char[len + 1];
    strncpy(fd_char, argument, len);
    // if the string before token is not a integer, return -2 as error
    if (atoi(fd_char) == 0 && strcmp(fd_char, "0") != 0)
        return -2;
    // else return value of descriptor
    return atoi(fd_char);
}

// return target file descriptor
int handleRedirection(char *argument)
{
    char *tokens[] = {"<", "&>>", "&>", ">>", ">"};
    for (int i = 0; i < 5; i++)
    {

        if (strstr(argument, tokens[i]) != NULL)
        {
            char *token_loc = strstr(argument, tokens[i]);
            int fd = -1;

            // find file name and filefd
            int fileFd = 0;
            int tokenLen = strlen(tokens[i]);
            char *filename = token_loc + tokenLen;

            // open file in different mode
            switch (i)
            {
            case 0:
                fileFd = open(filename, O_RDONLY);
                break;
            case 1:
            case 3:
                fileFd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
                break;
            case 2:
            case 4:
                fileFd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                break;
            }
            if (fileFd == -1)
            {
                perror("Error in opening or creating file");
                status = -1;
                return -1;
            }

            // if there might be fd, find fd
            if (i == 0 || i == 3 || i == 4)
            {
                fd = parseFd(argument, token_loc);
                // assign default fd if there not specified
                if (fd == -1)
                {
                    if (i == 0)
                        fd = 0;
                    else
                        fd = 1;
                }
                // report error if string before token is not a valid integer
                if (fd == -2)
                {
                    status = -1;
                    perror("string before token is not a valid int\n");
                    return -1;
                }
            }
            // in case of i = 1,2
            else
            {
                // if token if not at the begining
                if (token_loc != argument)
                {
                    status = -1;
                    perror("token is not at the beginning");
                    return -1;
                }
            }

            // use dup2 to deal with redirection
            if (i == 1 || i == 2)
            {
                dup2(fileFd, 1);
                dup2(fileFd, 2);
            }
            else if (dup2(fileFd, fd) == -1)
                perror("dup2 error\n");

            // close file after execute
            if (fileFd != -1)
            {
                close(fileFd);
            }
            return fileFd;
        }
    }

    // no tokens matched, no redirection
    return -2;
}

int handleBuiltinCase(char **arg_arr, ShellVar **head, int arg_num)
{
    char *builtin[7] = {"exit", "cd", "export", "local", "vars", "history", "ls"};
    for (int i = 0; i < 7; i++)
    {
        if (strcmp(arg_arr[0], builtin[i]) == 0)
        {
            // if not exit: set default return value = 0;
            if (i != 0)
                status = 0;
            switch (i)
            {
            case 0:
                if (arg_num != 1)
                {
                    status = -1;
                }
                else
                {
                    // free arguments
                    for (int i = 0; i < arg_num + 1; i++)
                    {
                        free(arg_arr[i]);
                    }
                    free(arg_arr);
                    // free shell variables
                    ShellVar *cur = *head;
                    while (cur != NULL)
                    {
                        free(cur->name);
                        free(cur->value);
                        ShellVar *prev = cur;
                        cur = cur->next;
                        free(prev);
                    }

                    // free history records
                    for (int i = 0; i < historyLen; i++)
                    {
                        if (historyRecord[i] == NULL)
                            break;
                        free(historyRecord[i]);
                    }
                    free(historyRecord);
                    historyRecord = NULL;

                    exit(status);
                }
                break;
            case 1:
                if (arg_num != 2)
                {
                    status = -1;
                }
                else
                {
                    cd(arg_arr);
                }
                break;
            case 2:
                if (arg_num != 2)
                {
                    status = -1;
                }
                else
                {
                    myExport(arg_arr);
                }
                break;
            case 3:
                if (arg_num != 2)
                {
                    status = -1;
                }
                else
                {
                    local(arg_arr, head);
                }

                break;
            case 4:
                if (arg_num != 1)
                {
                    status = -1;
                }
                else
                {
                    vars(*head);
                }
                break;
            case 5:
                history(arg_arr, arg_num);
                break;
            case 6:
                if (arg_num != 1)
                {
                    status = -1;
                }
                else
                {
                    ls();
                }
                break;
            }
            return 0;
        }
    }
    return -1;
}

char **parseArguments(char *command, int *num, int read)
{
    // Split token
    char *token = strtok(command, " ");

    // count the number of arguments
    int arg_num = 0;
    int com_index = 0;
    while (com_index < read)
    {
        if (command[com_index] != ' ' && command[com_index] != '\0')
        {
            arg_num++;
            while (command[com_index] != ' ' && command[com_index] != '\0')
            {
                com_index++;
            }
        }
        else
        {
            com_index++;
        }
    }

    // construct array of arguments
    char **arg_arr = malloc(sizeof(char *) * (arg_num + 1));
    int arg_index = 0;
    while (token != NULL)
    {
        arg_arr[arg_index] = strdup(token);
        token = strtok(NULL, " ");
        arg_index++;
    }
    arg_arr[arg_index] = NULL;
    *num = arg_num;
    return arg_arr;
}

// change variables name begin with $ to their value
int handleVariables(char ***arguments, ShellVar *head, int arg_num)
{
    for (int i = 0; i < arg_num; i++)
    {
        char *arg = NULL;
        char *var = NULL;
        if (seperateArgument(&arg, &var, (*arguments)[i], "$") == 0)
        {
            char *newArg = NULL;
            // check environment variables first
            if (getenv(var) != NULL)
            {
                newArg = (char *)malloc(strlen(arg) + strlen(getenv(var)) + 1);
                if (newArg == NULL)
                {
                    perror("newArg mem allocation in varaialbe transfer fails");
                    status = -1;
                    free(arg);
                    free(var);
                    free(newArg);
                    return -1;
                    ;
                }
                strcpy(newArg, arg);
                strcat(newArg, getenv(var));
            }
            // then check shell var
            else
            {
                ShellVar *cur = head;
                while (cur != NULL)
                {
                    if (strcmp(cur->name, var) == 0)
                    {
                        newArg = (char *)malloc(strlen(arg) + strlen(cur->value) + 1);
                        if (newArg == NULL)
                        {
                            perror("newArg mem allocation in varaialbe transfer fails");
                            status = -1;
                            free(arg);
                            free(var);
                            free(newArg);
                            return -1;
                        }
                        strcpy(newArg, arg);
                        strcat(newArg, cur->value);
                        break;
                    }
                    cur = cur->next;
                }
            }
            // if no var match
            if (newArg == NULL)
            {
                newArg = (char *)malloc(strlen(arg) + 1);
                strcpy(newArg, arg);
                newArg[strlen(arg)] = '\0';
            }
            free(arg);
            free(var);
            free((*arguments)[i]);
            (*arguments)[i] = newArg;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    // Default input is stdin
    FILE *input = stdin;

    // initial shell var
    ShellVar *head = NULL;
    // initial PATH
    setenv("PATH", "/bin", 1);

    // initial history record
    historyRecord = malloc(sizeof(char *) * historyLen);
    for (int i = 0; i < 5; i++)
    {
        historyRecord[i] = NULL;
    }

    // return error in case of multiple arguments
    if (argc > 2)
    {
        perror("Usage: ./wsh OR ./wsh batchFile\n");
        exit(-1);
    }

    // batch mode, take batch file as input
    if (argc == 2)
    {
        input = fopen(argv[1], "r");
        if (input == NULL)
            exit(-1);
    }

    char *command = NULL;
    size_t len = 0;
    ssize_t read;

    // store default file descriptor
    int default_fd[3];
    for (int i = 0; i < 3; i++)
    {
        default_fd[i] = dup(i);
    }

    // interactive loop
    while (1)
    {
        // default is not repeated
        isRepeated = 0;
        // restore default file descriptor
        for (int i = 0; i < 3; i++)
        {
            dup2(default_fd[i], i);
        }

        // store default file descriptor
        for (int i = 0; i < 3; i++)
        {
            default_fd[i] = dup(i);
        }
        // only in interactive mode needs to print prompt
        if (argc == 1)
        {
            printf("wsh> ");
        }

        // EOF
        if ((read = getline(&command, &len, input)) == -1)
        {
            free(command);
            break;
        }

        // skip the blank line
        if (read == 1)
        {
            continue;
        }

        // Remove the newline character from the input (if present)
        if (command[read - 1] == '\n')
        {
            command[read - 1] = '\0';
        }

        // store whole command for possible history record
        char record[read + 1];
        strcpy(record, command);

        // deal with command parsing
        int arg_num = 0;
        char **arg_arr = parseArguments(command, &arg_num, read);
        // free command after using.
        free(command);
        command = NULL;

        // ignore lines starting with #
        if (arg_arr[0][0] == '#')
        {

            // free arguments
            for (int i = 0; i < arg_num + 1; i++)
            {
                free(arg_arr[i]);
            }
            free(arg_arr);
            continue;
        }

        // transfer environ var and shell var to their values
        if (handleVariables(&arg_arr, head, arg_num) == -1)
        {

            // free arguments
            for (int i = 0; i < arg_num + 1; i++)
            {
                free(arg_arr[i]);
            }
            free(arg_arr);
            perror("error in handle variables");
            status = -1;
            continue;
        }

        // handle possible redirection
        int fileFd = -2;
        if (arg_num != 1)
        {
            fileFd = handleRedirection(arg_arr[arg_num - 1]);
        }
        // handle redirection error
        if (fileFd == -1)
        {

            // free arguments
            for (int i = 0; i < arg_num + 1; i++)
            {
                free(arg_arr[i]);
            }
            free(arg_arr);
            perror("error in redirection command\n");
            status = -1;
            continue;
        }
        // set redirect token to NULL if exist
        if (fileFd != -2 && fileFd != -1)
        {
            free(arg_arr[arg_num - 1]);
            arg_arr[arg_num - 1] = NULL;
            arg_num--;
        }

        // 1. Check if it's a builtin method
        int builtinCaseCheck = handleBuiltinCase(arg_arr, &head, arg_num);
        // if it is a valid "history <n>" command, then replace it with nth command in history record
        if (isRepeated != 0)
        {
            // get new command
            command = strdup(historyRecord[isRepeated - 1]);
            // free parsed old command
            for (int i = 0; i < arg_num; i++)
            {
                free(arg_arr[i]);
            }
            free(arg_arr);

            // parse new command
            arg_arr = parseArguments(command, &arg_num, strlen(command) + 1);
            free(command);
            command = NULL;

            // transfer environ var and shell var to their values
            if (handleVariables(&arg_arr, head, arg_num) == -1)
            {

                // free arguments
                for (int i = 0; i < arg_num + 1; i++)
                {
                    free(arg_arr[i]);
                }
                free(arg_arr);
                perror("error in handle variables");
                continue;
            }

            // handle possible redirection
            fileFd = -2;
            if (arg_num != 1)
            {
                fileFd = handleRedirection(arg_arr[arg_num - 1]);
            }
            // handle redirection error
            if (fileFd == -1)
            {

                // free arguments
                for (int i = 0; i < arg_num + 1; i++)
                {
                    free(arg_arr[i]);
                }
                free(arg_arr);
                perror("error in redirection command\n");
                continue;
            }
            // set redirect token to NULL if exist
            if (fileFd != -2 && fileFd != -1)
            {
                arg_arr[arg_num - 1] = NULL;
            }
        }
        if (builtinCaseCheck == -1 || isRepeated != 0)
        {
            // record history list
            if (isRepeated == 0)
            {
                // store only when it is not the same with first one
                if (historyRecord[0] == NULL || strcmp(historyRecord[0], record) != 0)
                {
                    for (int i = historyLen - 1; i > 0; i--)
                    {
                        if (historyRecord[i - 1] == NULL)
                        {
                            continue;
                        }
                        free(historyRecord[i]);
                        historyRecord[i] = strdup(historyRecord[i - 1]);
                    }
                    free(historyRecord[0]);
                    historyRecord[0] = strdup(record);
                }
            }
            // if not builtinCase, Fork a child process to execute the command
            pid_t pid = fork();
            // child process
            if (pid == 0)
            {
                // default success
                status = 0;

                // 2.check if it's a full or relative path
                // 3.if not check path specified by $PATH
                if (execv(arg_arr[0], arg_arr) != 0)
                {
                    char *paths = getenv("PATH");
                    char *path = strtok(paths, ":");
                    while (path != NULL)
                    {
                        char *fullPath = (char *)malloc(strlen(path) + strlen(arg_arr[0]) + 2);
                        strcpy(fullPath, path);
                        strcat(fullPath, "/");
                        strcat(fullPath, arg_arr[0]);
                        if (access(fullPath, X_OK) == 0)
                        {
                            execv(fullPath, arg_arr);
                        }

                        free(fullPath);
                        path = strtok(NULL, ":");
                    }
                }

                // if execv all fails
                status = -1;

                // free arguments
                for (int i = 0; i < arg_num + 1; i++)
                {
                    free(arg_arr[i]);
                }
                free(arg_arr);
                exit(-1);
            }
            // parent process
            else if (pid > 0)
            {
                wait(&status);
                if (WIFEXITED(status))
                {
                    status = WEXITSTATUS(status);
                }
            }
            // fork fails
            else
            {
                status = -1;
            }
        }

        // free arguments
        for (int i = 0; i < arg_num + 1; i++)
        {
            free(arg_arr[i]);
        }
        free(arg_arr);
    }

    // free shell variables
    ShellVar *cur = head;
    while (cur != NULL)
    {
        free(cur->name);
        free(cur->value);
        ShellVar *prev = cur;
        cur = cur->next;
        free(prev);
    }

    // free history records
    for (int i = 0; i < historyLen; i++)
    {
        if (historyRecord[i] == NULL)
            break;
        free(historyRecord[i]);
    }
    free(historyRecord);
    historyRecord = NULL;

    exit(status);
}