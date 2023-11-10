#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_JOBS 15
#define COMMAND_LENGTH 100

typedef struct
{
    pid_t pid;
    char command[COMMAND_LENGTH];
} Job;

int getcmd(char *prompt, char *args[], int *background)
{
    int length, i = 0;
    char *token, *loc;
    char *line = NULL;
    size_t linecap = 0;
    printf("%s", prompt);
    length = getline(&line, &linecap, stdin);
    if (length <= 0)
    {
        free(line);
        exit(-1);
    }
    // Check if background is specified..
    if ((loc = index(line, '&')) != NULL)
    {
        *background = 1;
        *loc = ' ';
    }
    else
        *background = 0;
    while ((token = strsep(&line, " \t\n")) != NULL)
    {
        for (int j = 0; j < strlen(token); j++)
            if (token[j] <= 32)
                token[j] = '\0';
        if (strlen(token) > 0)
            args[i++] = token;
    }
    free(line);
    line = NULL; // Reset the pointer after freeing
    return i;
}

/**
 * @brief Prints the arguments to the console.
 *
 * @param args array of arguments
 */
void runEcho(char *args[])
{
    for (int i = 1; args[i]; i++)
    {
        printf("%s ", args[i]);
    }
}

/**
 * @brief Prints the current working directory to the console.
 *
 */
void runPwd()
{
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    printf("%s\n", cwd);
}

/**
 * @brief Changes the current working directory to the specified directory. If no directory is specified, prints the current working directory.
 *
 * @param args array of arguments
 */
void runCd(char *args[])
{
    if (args[1])
    {
        chdir(args[1]);
    }
    else
    {
        runPwd();
    }
}

/**
 * @brief Removes a job from the jobs array.
 *
 * @param jobs jobs array
 * @param jobCount number of jobs
 * @param pid pid of the job to be removed
 * @return int -1 if not found, 0 if success
 */
int removeJob(Job *jobs, int *jobCount, pid_t pid)
{
    for (int i = 0; i < *jobCount; i++)
    {
        if (jobs[i].pid == pid)
        {
            for (int j = i; j < *jobCount - 1; j++)
            {
                jobs[j] = jobs[j + 1];
            }
            *jobCount = *jobCount - 1;
            return 0;
        }
    }

    return -1;
}

/**
 * @brief Adds a job to the jobs array.
 *
 * @param jobs jobs array
 * @param jobCount number of jobs
 * @param pid pid of the job to be added
 * @param command command of the job to be added
 * @return int -1 if error, 0 if success
 */
int addJob(Job *jobs, int *jobCount, pid_t pid, char *command)
{
    if (*jobCount == MAX_JOBS)
    {
        perror("Max job limit reached\n");
        return -1;
    }

    if (strlen(command) >= COMMAND_LENGTH)
    {
        perror("Command entered is too long\n");
        return -1;
    }

    strcpy(jobs[*jobCount].command, command);
    jobs[*jobCount].pid = pid;
    *jobCount += 1;
    return 0;
}

/**
 * @brief Prints the list of background jobs.
 *
 * @param jobs jobs array
 * @param jobCount number of jobs
 */
void listJobs(Job jobs[], int *jobCount)
{
    printf("Job count: %d\n", *jobCount);
    for (int i = 0; i < *jobCount; i++)
    {
        printf("[%d] %s\t%d\n", i + 1, jobs[i].command, jobs[i].pid);
    }
}

/**
 * @brief Bring a background job to the foreground. Waits for the job to finish and then removes it from the jobs array.
 *
 * @param jobs jobs array
 * @param jobCount number of jobs
 * @param jobNumberStr job number to bring to the foreground (in string format)
 * @return int -1 if error, 0 if success
 */
int runFg(Job jobs[], int *jobCount, char *jobNumberStr)
{
    if (jobNumberStr == NULL)
    {
        perror("Job number not specified\n");
        return -1;
    }

    // convert job number to int
    int jobNumber = atoi(jobNumberStr);

    // check if job number is valid
    if (jobNumber > *jobCount || jobNumber < 1)
    {
        perror("Job number does not exist\n");
        return -1;
    }

    // wait for the job to finish
    int status;
    pid_t pid = jobs[jobNumber - 1].pid;
    waitpid(pid, &status, 0);

    // remove the job from the jobs array
    int removal = removeJob(jobs, jobCount, pid);
    if (removal == -1)
    {
        perror("Could not find job during removal.\n");
        return -1;
    }
    return 0;
}

/**
 * @brief Checks if any background jobs have finished. If a background job has finished, it is removed from the jobs array.
 *
 * @param jobs array of jobs
 * @param jobCount number of jobs
 * @return int -1 if error, 0 if success
 */
int checkJobs(Job *jobs, int *jobCount)
{
    for (int i = 0; i < *jobCount; i++)
    {
        if (waitpid(jobs[i].pid, NULL, WNOHANG) > 0)
        {
            int removal = removeJob(jobs, jobCount, jobs[i].pid);
            if (removal == -1)
            {
                perror("Could not find job during removal.\n");
                return -1;
            }
            // to fix the index now that jobs has one element less
            i--;
        }
    }
    return 0;
}

/**
 * @brief Handles pipe commands. Creates a pipe and forks twice. The first child executes the first command and writes to the pipe. The second child executes the second command and reads from the pipe.
 *
 * @param args array of arguments
 */
void handlePipe(char *args[])
{
    int fd[2];
    if (pipe(fd) < 0)
    {
        perror("Error while creating pipe");
        exit(-1);
    }

    pid_t pid = fork();

    if (pid == 0)
    {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);

        // new args for the first command
        char *left_args[20];
        memset(left_args, 0, sizeof(left_args));
        for (int i = 0; strcmp(args[i], "|") != 0; i++)
        {
            left_args[i] = args[i];
        }

        execvp(left_args[0], left_args);
        perror("Failed to run first command of pipe.");
        exit(-1);
    }

    // waiting for the first command to finish
    int status;
    waitpid(pid, &status, 0);

    close(fd[1]);

    pid = fork();
    if (pid == 0)
    {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);

        // args for the second command
        char *right_args[20];
        memset(right_args, 0, sizeof(right_args));
        int i = 0;
        for (; strcmp(args[i], "|") != 0; i++)
        {
            // skip the first command
        }
        i++;
        int j = 0;
        for (; args[i]; i++, j++)
        {
            right_args[j] = args[i];
        }

        execvp(right_args[0], right_args);
        perror("execvp");
        exit(-1);
    }
    waitpid(pid, &status, 0);
    close(fd[0]);
}

/**
 * @brief Checks if a pipe exists in the command. If it does, returns the command after the pipe.
 *
 * @param args array of arguments
 * @return char* the command after the pipe. NULL if no pipe exists.
 */
char *pipeExists(char *args[])
{
    for (int i = 0; args[i]; i++)
    {
        if (strcmp(args[i], "|") == 0)
        {
            return args[i + 1];
        }
    }
    return NULL;
}

/**
 * @brief Removes the redirection type from the args array.
 *
 * @param args array of arguments
 * @param index index of the redirection type in the args array
 */
void removeRedirection(char *args[], int index)
{
    for (int i = index; args[i]; i++)
    {
        // we skip two indeces because we want to remove the redirection type AND the file name
        args[i] = args[i + 2];
    }
}

/**
 * @brief Handles output redirection. Opens the file and redirects stdout to the file.
 *
 * @param args array of arguments containing redirection type and file name
 */
void handleOutputRedirection(char *args[])
{
    // check if output redirection exists: > or >> is in args[0]
    int flag = strcmp(args[0], ">>") == 0 ? O_APPEND : O_TRUNC;
    // file name contained in args[1]
    int fd = open(args[1], O_WRONLY | O_CREAT | flag, 0644);
    if (fd < 0)
    {
        perror("Error while opening or creating file.");
        exit(-1);
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
}

/**
 * @brief Determines if output redirection exists in the command. Cleans up > or >> from args if command contains output redirection.
 *
 * @param args array of arguments
 * @return char** result array containing the redirection type (result[0]) and the file name (result[1]). NULL if no output redirection exists.
 */
char **outputRedirectionExists(char *args[])
{
    static char *result[2];
    for (int i = 0; args[i]; i++)
    {
        if (strcmp(args[i], ">") == 0 || strcmp(args[i], ">>") == 0)
        {
            // redirection type: >> vs >
            result[0] = args[i];
            // file name
            result[1] = args[i + 1];

            // if output redirection exists, then > or >> must be removed from args
            removeRedirection(args, i);

            return result;
        }
    }
    return NULL;
}

/**
 * @brief Handles input redirection. Opens the file and redirects stdin to the file.
 *
 * @param args
 */
void handleInputRedirection(char *args)
{
    // check if input redirection exists: < is in args[0]
    int fd = open(args , O_RDONLY);
    if (fd < 0)
    {
        perror("Error while opening file.");
        exit(-1);
    }
    dup2(fd, STDIN_FILENO);
    close(fd);
}

/**
 * @brief Determines if input redirection exists in the command. Cleans up < from args if command contains input redirection.
 *
 * @param args array of arguments
 * @return char* the file name. NULL if no input redirection exists.
 */
char *inputRedirectionExists(char *args[])
{
    for (int i = 0; args[i]; i++)
    {
        if (strcmp(args[i], "<") == 0)
        {
            // result is the file name
            char *result = args[i + 1];

            // if input redirection exists, then < must be removed from args
            removeRedirection(args, i);

            return result;
        }
    }
    return NULL;
}

int main(void)
{
    char *args[20];
    Job jobs[MAX_JOBS];
    int jobCount = 0;
    memset(jobs, 0, sizeof(jobs));
    int bg = 0;
    pid_t pid = 0;
    int iterations = 0;
    char *pipe_command = NULL;
    char **outputRedirection;
    char *inputRedirection;
    int defaultInput = -1;
    int defaultOutput = -1;
    while (1)
    {
        // check if any background jobs have finished
        checkJobs(jobs, &jobCount);

        // at every iteration, clean the args array before getting new input
        memset(args, 0, sizeof(args));
        int cnt = getcmd("\n>> ", args, &bg);

        // if no command is entered, continue
        if (cnt == 0)
            continue;

        // if the command is exit, quit the shell
        if (strcmp(args[0], "exit") == 0)
        {
            exit(0);
        }
        // built-in commands
        else if (strcmp(args[0], "pwd") == 0)
        {
            runPwd();
        }
        else if (strcmp(args[0], "cd") == 0)
        {
            runCd(args);
        }
        else if (strcmp(args[0], "echo") == 0)
        {
            runEcho(args);
        }
        else if (strcmp(args[0], "jobs") == 0)
        {
            listJobs(jobs, &jobCount);
        }
        else if (strcmp(args[0], "fg") == 0)
        {
            runFg(jobs, &jobCount, args[1]);
        }
        // not a built-in command
        else
        {
            // Check if the command contains a pipe
            pipe_command = pipeExists(args);

            // check if output/input redirection exists
            outputRedirection = outputRedirectionExists(args);
            inputRedirection = inputRedirectionExists(args);

            // if there was any redirection, we need to save default input and output (stdin and stdout) to then reset them
            if (outputRedirection || inputRedirection) {
                defaultInput = dup(STDIN_FILENO);
                defaultOutput = dup(STDOUT_FILENO);
                if (defaultInput < 0 || defaultOutput < 0) {
                    perror("Error while saving default input and output");
                    exit(-1);
                }
            }

            if (outputRedirection)
            {
                handleOutputRedirection(outputRedirection);
            }

            if (inputRedirection)
            {
                handleInputRedirection(inputRedirection);
            }

            pid = fork();

            // child process
            if (pid == 0)
            {
                if (pipe_command)
                {
                    handlePipe(args);
                }
                else
                {
                    execvp(args[0], args);
                    perror("Command execution failed");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                // (3) if background is not specified, the parent will wait,
                // otherwise parent starts the next command...
                if (bg == 0)
                {
                    int status;
                    waitpid(pid, &status, 0);
                }
                else
                {
                    int addingJob = addJob(jobs, &jobCount, pid, args[0]);
                    if (addingJob == -1)
                    {
                        perror("Error while adding job");
                        exit(-1);
                    }
                }
            }

            // if there was any redirection, we need to reset stdin and stdout
            if (outputRedirection || inputRedirection) {
                dup2(defaultInput, STDIN_FILENO);
                dup2(defaultOutput, STDOUT_FILENO);
                close(defaultInput);
                close(defaultOutput);
            }
        }
    }
}