#include "systemcalls.h"
#include <stdlib.h>
#include <unistd.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    int status = system(cmd);
    
    if(status == -1)
    {    
        printf("Error: system() failed");
        return false;
    }
    else
    {
        return true;
    }
}

/**

* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    int return_status = false;
    
    pid_t child_pid = fork();
    
    // On failure, -1 is returned in the parent
    if (child_pid == -1)
    {
        printf("Fork failed, no child process is created");

        return_status = false;
    }

    // 0 is returned in the child
    else if (child_pid == 0)
    {
        // This code is executed by the child process.
        printf("Child process is created created successfully\n");
        
        int status = execv(command[0], command);
        
        if(status == -1)
        {    
            printf("Error: execv() failed");
            return_status = false;
        }
    }
    // runs for parent
    else 
    {
        int wstatus, status;
        status = waitpid(child_pid, &wstatus, 0);

        if(status == -1)
        {    
            printf("Error: waitpid() failed");
            return_status = false;
        }
        else
        {
            // true if the child terminated normally
            if (WIFEXITED(wstatus)) 
            {
                return_status = true;
                printf("exited, status=%d\n", WEXITSTATUS(wstatus));
            }
            else
            {
                return_status = false;
            }
        }
    }
    
    va_end(args);

    return return_status;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

    va_end(args);

    return true;
}
