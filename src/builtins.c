#include <string.h>
#include "builtins.h"
#include "io_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>        // For gethostbyname()
#include <arpa/inet.h>    // For inet_ntoa() and htons()
#include <netinet/in.h>

#define MAX_BG_PROCESSES 1024

static pid_t server_pid = 0;

int execute_system_command(char **cmd);

// Structure to track background processes.
typedef struct {
    int job_number;
    pid_t pid;
    char command[1024];
} BackgroundProcess;


BackgroundProcess bg_processes[MAX_BG_PROCESSES];
int bg_count = 0;

void sigchld_handler(int signum) {
    (void)signum; // Suppress unused parameter warning

    int status;
    pid_t pid;
    char msg[1024];

    // Process all finished children without blocking.
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Find the corresponding background job.
        for (int i = 0; i < bg_count; i++) {
            if (bg_processes[i].pid == pid) {
                if (WIFSIGNALED(status)) {
                    // Process was terminated by a signal.
                    snprintf(msg, sizeof(msg), "[%d]+  Done: %.900s\n",
                             bg_processes[i].job_number, bg_processes[i].command);
                } else {
                    // Process exited normally.
                    snprintf(msg, sizeof(msg), "[%d]+  Done\n",
                             bg_processes[i].job_number);
                }
                // Write the message directly (async-signal–safe).
                write(STDOUT_FILENO, msg, strlen(msg));

                // Remove the finished job from the list.
                for (int j = i; j < bg_count - 1; j++) {
                    bg_processes[j] = bg_processes[j + 1];
                }
                bg_count--;
                break;
            }
        }
    }
}


// ====== Command execution =====

/* Return: index of builtin or -1 if cmd doesn't match a builtin
 */
bn_ptr check_builtin(const char *cmd) {
    ssize_t cmd_num = 0;
    while (cmd_num < BUILTINS_COUNT &&
           strncmp(BUILTINS[cmd_num], cmd, MAX_STR_LEN) != 0) {
        cmd_num += 1;
    }
    return BUILTINS_FN[cmd_num];
}


// ===== Builtins =====

/* Prereq: tokens is a NULL terminated sequence of strings.
 * Return 0 on success and -1 on error ... but there are no errors on echo. 
 */
ssize_t bn_echo(char **tokens) {
    ssize_t index = 1;

    if (tokens[index] != NULL) {
        // TODO:
        // Implement the echo command
	display_message(tokens[index]);
	index += 1;
    }
    while (tokens[index] != NULL) {
        // TODO:
        // Implement the echo command
	display_message(" ");
	display_message(tokens[index]);
        index += 1;
    }
    display_message("\n");

    return 0;
}


// Forward declarations for helper functions
void ls_list(const char *path, const char *filter);
void ls_recursive(const char *path, int current_depth, int max_depth, int use_depth_limit, const char *filter);


/*
 * ls_list - Lists all entries in a single directory.
 * If a filter is provided, only entries containing the substring are printed.
 * This version does not print "." and ".." unless they match the filter.
 */
void ls_list(const char *path, const char *filter) {
    DIR *d = opendir(path);
    if (!d) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        // Apply filtering if needed.
        if (filter != NULL && strstr(entry->d_name, filter) == NULL)
            continue;
        
        // Print the directory entry (including "." and "..").
        display_message(entry->d_name);
        display_message("\n");
    }
    
    closedir(d);
}





/*
 * ls_recursive - Recursively lists directory contents.
 *
 * Parameters:
 *   path           - Current directory to list.
 *   current_depth  - Current recursion depth.
 *   max_depth      - Maximum recursion depth limit.
 *   use_depth_limit- If non-zero, the recursion is limited by max_depth.
 *   filter         - If provided, only entries matching the filter are printed.
 */

void ls_recursive(const char *path, int current_depth, int max_depth, int use_depth_limit, const char *filter) {
    // If we somehow exceed the depth (should not happen because recursion is
    // guarded), then return.
    if (use_depth_limit && current_depth > max_depth) {
        return;
    }

    DIR *d = opendir(path);
    if (!d)
        return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        /*
          Print the entry if:
          - No filter was provided, or
          - The entry's name contains the filter substring.
        */
        if (filter == NULL || strstr(entry->d_name, filter) != NULL) {
            display_message(entry->d_name);
            display_message("\n");
        }

        /* 
          For recursion:
          - We do not descend into the special "." and ".." directories.
          - If the entry is a directory and we haven't reached max depth,
            recurse into it.
          Note: We always recurse regardless of whether this directory’s name
          passed the filter because inner entries might match even if its parent's
          name does not.
        */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Build full path for the entry.
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat statbuf;
        if (stat(full_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
            if (!use_depth_limit || current_depth < max_depth) {
                ls_recursive(full_path, current_depth + 1, max_depth, use_depth_limit, filter);
            }
        }
    }
    closedir(d);
}

/*
  bn_ls: the built-in ls command.
  Usage: ls [path] [--f substring] [--rec] [--d depth]
  If recursive mode is on, use ls_recursive starting at depth 1.
  A supplied depth of 1 means only the top-level directory (depth 1) is listed.
*/
ssize_t bn_ls(char **tokens) {
    char *dir_path = ".";
    int recursive = 0;
    int max_depth = -1;  // -1 indicates no limit.
    const char *filter = NULL;

    int i = 1;
    while (tokens[i] != NULL) {
        if (strcmp(tokens[i], "--rec") == 0) {
            recursive = 1;
            i++;
        } else if (strcmp(tokens[i], "--d") == 0) {
            i++;
            if (tokens[i] == NULL) {
                display_error("ERROR: --d requires a depth value", "");
                return -1;
            }
            max_depth = atoi(tokens[i]);
            // A depth of less than 1 is invalid.
            if (max_depth < 1) {
                display_error("ERROR: Invalid depth value: ", tokens[i]);
                return -1;
            }
            i++;
        } else if (strcmp(tokens[i], "--f") == 0) {
            i++;
            if (tokens[i] == NULL) {
                display_error("ERROR: --f requires a substring filter", "");
                return -1;
            }
            filter = tokens[i];
            i++;
        } else {
            dir_path = tokens[i];
            i++;
        }
    }

    DIR *d = opendir(dir_path);
    if (!d) {
        display_error("ERROR: Invalid path: ", dir_path);
        return -1;
    }
    closedir(d);

    if (recursive) {
        if (max_depth == -1)
            ls_recursive(dir_path, 1, 999, 1, filter); // Use a large number when no limit.
        else
            ls_recursive(dir_path, 1, max_depth, 1, filter);
    }
    else {
        // Assume ls_list is defined elsewhere for non-recursive listing.
	ls_list(dir_path, filter);
    }

    fflush(stdout);
    return 0;
}




  















/*
 * bn_cat - Builtin function for the "cat" command.
 *
 * Usage: cat [file]
 *
 * Displays the contents of the file to stdout.
 * If no file is provided, report:
 *    ERROR: No input source provided
 *
 * If the file cannot be opened, report:
 *    ERROR: Cannot open file: [file]
 *
 * Returns 0 on success and -1 on error.
 */
ssize_t bn_cat(char **tokens) {
    // Check if a file name is provided
    if (tokens[1] == NULL) {
        // Read from stdin if no file name is given
        if (isatty(STDIN_FILENO)) { // Check if stdin is coming from terminal or a pipe
            display_error("ERROR: No input source provided", "");
            return -1;
        } else {
            // Read and display from stdin
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
                display_message(buffer);
            }
            return 0;
        }
    }

    // Open the file for reading
    FILE *fp = fopen(tokens[1], "r");
    if (fp == NULL) {
        display_error("ERROR: Cannot open file: ", tokens[1]);
        return -1;
    }

    // Read and display the file line by line
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        display_message(line);
    }

    fclose(fp);
    return 0;
}

ssize_t bn_wc(char **tokens) {
    FILE *fp = NULL;

    // Determine input source
    if (tokens[1] == NULL) {
        // Use stdin if no file name is provided
        if (isatty(STDIN_FILENO)) { // Check if stdin is coming from terminal or a pipe
            display_error("ERROR: No input source provided", "");
            return -1;
        } else {
            fp = stdin;
        }
    } else {
        // Open the file for reading
        fp = fopen(tokens[1], "r");
        if (fp == NULL) {
            display_error("ERROR: Cannot open file: ", tokens[1]);
            return -1;
        }
    }

    // Initialize counters
    unsigned long word_count = 0;
    unsigned long char_count = 0;
    unsigned long newline_count = 0;
    int in_word = 0;
    int c;

    // Read input character by character
    while ((c = fgetc(fp)) != EOF) {
        char_count++;  // Count every character

        if (c == '\n') {
            newline_count++;
        }

        // Determine if the character is whitespace
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            in_word = 0;
        } else {
            if (!in_word) { // Transition into a new word
                word_count++;
                in_word = 1;
            }
        }
    }

    // Close the file if reading from a file
    if (fp != stdin) {
        fclose(fp);
    }

    // Display results
    char buffer[256];
    sprintf(buffer, "word count %lu\n", word_count);
    display_message(buffer);
    sprintf(buffer, "character count %lu\n", char_count);
    display_message(buffer);
    sprintf(buffer, "newline count %lu\n", newline_count);
    display_message(buffer);

    return 0;
}


/*
 * bn_cd - Builtin function for "cd" command.
 *
 * Usage: cd [path]
 *  - If no path is provided, defaults to the HOME environment variable.
 *  - Supports relative paths with:
 *      .    -> current directory
 *      ..   -> parent directory (standard behavior)
 *      ...  -> two directories up (equivalent to "../..")
 *      .... -> three directories up (equivalent to "../../..")
 *
 * Returns 0 on success and -1 on error.
 */
ssize_t bn_cd(char **tokens) {
    char *path = NULL;

    // If no path is provided, use HOME.
    if (tokens[1] == NULL) {
        path = getenv("HOME");
        if (path == NULL) {
            display_error("ERROR: HOME not set", "");
            return -1;
        }
    } else {
        path = tokens[1];
    }

    // Handle special dot notation for multiple parent directories.
    const char *new_path = path;
    if (strcmp(path, "...") == 0) {
        new_path = "../..";
    } else if (strcmp(path, "....") == 0) {
        new_path = "../../..";
    }

    // Attempt to change directory.
    if (chdir(new_path) == -1) {
        display_error("ERROR: Invalid path: ", path);
        return -1;
    }

    // Flush standard output to ensure no buffered output delays the prompt.
    fflush(stdout);
    return 0;
}

void execute_pipe(char **cmd1, char **cmd2) {
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(1);
    }

    pid_t pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        exit(1);
    }

    if (pid1 == 0) { // First child process
        close(pipe_fd[0]); // Close unused read end
        dup2(pipe_fd[1], STDOUT_FILENO); // Redirect stdout to write end of the pipe
        close(pipe_fd[1]);
	
	if (strchr(cmd1[0], '=') != NULL) {
    		exit(0);
	}


        // Check if the first command is a built-in
        bn_ptr builtin_fn = check_builtin(cmd1[0]);
        if (builtin_fn != NULL) {
            // Call the built-in function
            ssize_t err = builtin_fn(cmd1);
            if (err == -1){
            	display_error("ERROR: Builtin failed: ", cmd1[0]);
	    }
            exit(0); // Exit after built-in execution
        }
	else if (builtin_fn == NULL){
	    if (execute_system_command(cmd1) == -1) {
		display_error("ERROR: Unknown command: ", cmd1[0]);
        	}
	}
    }

    pid_t pid2 = fork();
    if (pid2 == -1) {
        perror("fork");
        exit(1);
    }

    if (pid2 == 0) { // Second child process
        close(pipe_fd[1]); // Close unused write end
        dup2(pipe_fd[0], STDIN_FILENO); // Redirect stdin to read end of the pipe
        close(pipe_fd[0]);

        // Check if the second command is a built-in
        bn_ptr builtin_fn = check_builtin(cmd2[0]);
        if (builtin_fn != NULL) {
            // Call the built-in function
            ssize_t err = builtin_fn(cmd2);
            if (err == -1){
                display_error("ERROR: Builtin failed: ", cmd2[0]);
		}

            exit(0); // Exit after built-in execution
        }
	else if (builtin_fn == NULL){
	    if (execute_system_command(cmd2) == -1) {
		display_error("ERROR: Unknown command: ", cmd2[0]);
        	}
	}
    }

    // Parent process closes pipe and waits for both children
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}


int start_background_process(char **cmd) {
    sigset_t mask, prev_mask;
    // Prepare mask to block SIGCHLD.
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    // Block SIGCHLD to prevent race conditions on bg_processes and bg_count.
    if (sigprocmask(SIG_BLOCK, &mask, &prev_mask) == -1) {
        perror("sigprocmask");
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        // Fork error: restore signal mask and return error.
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // Child process: restore the signal mask so that signals are received normally.
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        execvp(cmd[0], cmd);
        // If execvp returns, there was an error.
        perror("execvp");
        _exit(1);  // Use _exit to avoid flushing the parent's I/O buffers.
    } else {
        // Parent process: safely update bg_processes and bg_count.
        if (bg_count >= MAX_BG_PROCESSES) {
            fprintf(stderr, "ERROR: Too many background processes\n");
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);  // Restore signal mask.
            return -1;
        }

        int job_number = bg_count + 1;
        bg_processes[bg_count].job_number = job_number;
        bg_processes[bg_count].pid = pid;

        // Concatenate tokens to form the full command string.
        char cmd_str[1024] = {0};
        for (int i = 0; cmd[i] != NULL; i++) {
            size_t remaining_space = sizeof(cmd_str) - strlen(cmd_str) - 1;
            strncat(cmd_str, cmd[i], remaining_space);
            if (cmd[i + 1] != NULL) {
                remaining_space--;
                strncat(cmd_str, " ", remaining_space);
            }
        }
        strncpy(bg_processes[bg_count].command, cmd_str, sizeof(bg_processes[bg_count].command) - 1);
        bg_count++;

        // Restore the previous signal mask (unblock SIGCHLD).
        if (sigprocmask(SIG_SETMASK, &prev_mask, NULL) == -1) {
            perror("sigprocmask");
            return -1;
        }

        // Display the background job creation message.
        char message[128];
        snprintf(message, sizeof(message), "[%d] %d\n", job_number, pid);
        write(STDOUT_FILENO, message, strlen(message));
    }

    return 0; // Success.
}




// Function to check if a string represents a valid integer
int is_number(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (!isdigit(str[i])) {
            return 0;
        }
    }
    return 1;
}

ssize_t handle_kill_command(char **tokens) { 
    if (tokens[1] == NULL) {
        fprintf(stderr, "ERROR: Invalid usage. Format: kill [pid] [signum]\n");
	fflush(stderr);
        return -1;
    }

    if (!is_number(tokens[1])) {
        display_error("ERROR: Invalid process ID: ", tokens[1]);
        return -1;
    }
    pid_t pid = atoi(tokens[1]);

    int signum = SIGTERM; // Default signal
    if (tokens[2] != NULL) {
        if (!is_number(tokens[2])) {
    	    display_error("ERROR: Invalid signal specified: ", tokens[2]);
            return -1;
        }
        signum = atoi(tokens[2]);
        if (signum < 1 || signum >= NSIG) {
            display_error("ERROR: Invalid signal specified: ",tokens[2]);
            return -1;
        }
    }

    if (kill(pid, signum) == -1) {
        if (errno == ESRCH) {
            fprintf(stderr, "ERROR: The process does not exist\n");;
	    fflush(stderr);
        } else if (errno == EPERM) {
            fprintf(stderr, "ERROR: Permission denied\n");
	    fflush(stderr);
        } else {
            perror("kill");
        }
        return -1;
    }

    return 0;
}

// Function to handle the `ps` command
ssize_t handle_ps_command(char **tokens) {
    char message[1100]; 
    (void)tokens; // Suppress unused parameter warning since `ps` has no arguments

    if (bg_count == 0) {
        display_message("No background processes.\n");
        return 0;
    }

    // Iterate over background processes and display their details
    for (int i = 0; i < bg_count; i++) {
        snprintf(message, sizeof(message), "%s %d\n", bg_processes[i].command, bg_processes[i].pid);
        display_message(message);
    }

    return 0; // Success
}


/**
 * Executes a system command by searching in /bin, /usr/bin, or other
 * directories in the PATH environment variable.
 * 
 * @param cmd Array of command and arguments (e.g., {"ls", "-l", NULL}).
 * @return 0 on success, -1 on failure.
 */
int execute_system_command(char **cmd) {
    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // In the child process: execute the command
        execvp(cmd[0], cmd);
        exit(1); // Exit child process on failure
    } else {
        // In the parent process: wait for the child process to finish
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            return -1;
        }
        // Check if the child process exited successfully
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0; // Success
        } else {
            return -1; // Failure
        }
    }
}

// Forward declaration of your server's run loop.
extern void run_server(int port);

ssize_t start_server_builtin(char **tokens) {
    if (tokens[1] == NULL) {
        write(STDERR_FILENO, "ERROR: No port provided\n", 24);
        return -1;
    }

    int port = atoi(tokens[1]);
    if (port <= 0) {
	display_error("ERROR: Invalid port number: ", tokens[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // Child process: detach and run the server.
        setsid();
        signal(SIGINT, SIG_IGN);
        run_server(port);
        exit(0);
    } else {
        // Save the server's PID to our static variable.
        server_pid = pid;
        printf("Server started on port %d with PID %d\n", port, pid);
    }
    return 0;
}

/*
 * close_server_builtin:
 * Implements the "close-server" command.
 * Terminates the server process previously started by "start-server".
 *
 * Usage: close-server
 */
ssize_t close_server_builtin(char **tokens) {
    (void)tokens;  // Unused parameter if there are no further tokens.

    if (server_pid == 0) {
        // No server has been started.
        write(STDERR_FILENO, "ERROR: No server is running\n", 29);
        return -1;
    }

    // Attempt to terminate the server by sending it a SIGTERM.
    if (kill(server_pid, SIGTERM) == -1) {
        perror("kill");
        return -1;
    }

    printf("Server with PID %d terminated.\n", server_pid);
    // Optionally, you could wait for the process to finish cleanup here.
    server_pid = 0;   // Reset the server PID
    return 0;
}


/*
 * send_builtin:
 *
 * Implements the "send" command.
 * Syntax: send port-number hostname message
 *
 * - Checks that a port and hostname are provided.
 * - Constructs the message from tokens[3] onward.
 * - Establishes a TCP connection to the given hostname at the port.
 * - Sends the message.
 * - The server (which you started earlier) should then print the message
 *   to its console and broadcast it to all connected clients.
 */
ssize_t send_builtin(char **tokens) {
    // Error-checking for port number.
    if (tokens[1] == NULL) {
        write(STDERR_FILENO, "ERROR: No port provided\n", 24);
        return -1;
    }
    // Error-checking for hostname.
    if (tokens[2] == NULL) {
        write(STDERR_FILENO, "ERROR: No hostname provided\n", 28);
        return -1;
    }

    int port = atoi(tokens[1]);
    if (port <= 0) {
        fprintf(stderr, "ERROR: Invalid port number: %s\n", tokens[1]);
        return -1;
    }

    char *hostname = tokens[2];

    // Verify that a message is provided.
    if (tokens[3] == NULL) {
        write(STDERR_FILENO, "ERROR: No message provided\n", 27);
        return -1;
    }

    // Construct the message by concatenating tokens from index 3 onward.
    char message[1024] = "";
    for (int i = 3; tokens[i] != NULL; i++) {
        strcat(message, tokens[i]);
        if (tokens[i + 1] != NULL) {  // Add a space if this is not the last token.
            strcat(message, " ");
        }
    }

    // Create a TCP socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Resolve the hostname.
    struct hostent *server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR: No such host: %s\n", hostname);
        close(sockfd);
        return -1;
    }

    // Set up the server address structure.
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // Copy the resolved IP address into the server address.
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    // Connect to the server.
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    // Send the message.
    if (send(sockfd, message, strlen(message), 0) < 0) {
        perror("send");
        close(sockfd);
        return -1;
    }

    // Close the socket after sending.
    close(sockfd);

    return 0;
}



#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>        // For gethostbyname()
#include <arpa/inet.h>    // For htons(), inet_ntoa()
#include <netinet/in.h>
#include <pthread.h>
#include "builtins.h"     // Your built-in function prototypes
#include "io_helpers.h"   // For any helper output functions

#define CLIENT_BUFFER_SIZE 1024

// This thread continually reads data from the server and prints it.
static void *receive_thread(void *arg) {
    int sockfd = *(int *)arg;
    char buffer[CLIENT_BUFFER_SIZE];
    ssize_t n;

    while (1) {
        n = read(sockfd, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            // Either the connection is closed or there is an error.
            break;
        }
        buffer[n] = '\0';
        // Print any received data.
        printf("%s", buffer);
        fflush(stdout);
    }
    return NULL;
}

/*
 * start_client_builtin
 *
 * Implements the "start-client" command.
 * Syntax: start-client port-number hostname
 *
 * Behavior:
 *   - Reports an error if no port or hostname is provided.
 *   - Creates a single TCP connection to the given host and port.
 *   - Reads the initial welcome message from the server (which assigns the client an ID).
 *   - Starts a receiving thread for incoming messages.
 *   - In the main thread, reads standard input line by line and sends each message
 *     prefixed with the client’s ID (extracted from the welcome message) to the server.
 *   - Special messages like "\connected" are handled by the server.
 */
ssize_t start_client_builtin(char **tokens) {
    // Error-check parameters.
    if (tokens[1] == NULL) {
        write(STDERR_FILENO, "ERROR: No port provided\n", 24);
        return -1;
    }
    if (tokens[2] == NULL) {
        write(STDERR_FILENO, "ERROR: No hostname provided\n", 28);
        return -1;
    }

    int port = atoi(tokens[1]);
    if (port <= 0) {
        fprintf(stderr, "ERROR: Invalid port number: %s\n", tokens[1]);
        return -1;
    }
    char *hostname = tokens[2];

    // Create a TCP socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Resolve the hostname.
    struct hostent *server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR: No such host: %s\n", hostname);
        close(sockfd);
        return -1;
    }

    // Set up the server address.
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // Copy the resolved IP address.
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    // Connect to the server.
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    // Read the welcome message from the server.
    // Expected format (from our server): "You are clientX:\n"
    char welcome[CLIENT_BUFFER_SIZE];
    ssize_t n = read(sockfd, welcome, sizeof(welcome) - 1);
    if (n <= 0) {
        perror("read");
        close(sockfd);
        return -1;
    }
    welcome[n] = '\0';
    // Print the welcome message.
    printf("%s", welcome);

    // Extract the client ID prefix from the welcome message.
    // We expect the welcome message to start with "You are clientX:"
    char client_prefix[64] = "";
    if (sscanf(welcome, "You are %63s", client_prefix) != 1) {
        // Fallback if parsing fails.
        strcpy(client_prefix, "client?:");
    }
    // Remove a possible trailing newline.
    size_t len = strlen(client_prefix);
    if (len > 0 && client_prefix[len - 1] == '\n')
        client_prefix[len - 1] = '\0';

    // Start a receiver thread that keeps printing any incoming messages.
    pthread_t recv_tid;
    if (pthread_create(&recv_tid, NULL, receive_thread, &sockfd) != 0) {
        perror("pthread_create");
        close(sockfd);
        return -1;
    }

    // Read messages from standard input in the main thread.
    char input[CLIENT_BUFFER_SIZE];
    while (fgets(input, sizeof(input), stdin) != NULL) {
        // Remove the newline at the end, if present.
        size_t input_len = strlen(input);
        if (input_len > 0 && input[input_len - 1] == '\n')
            input[input_len - 1] = '\0';

        // Calculate the total message length to ensure it fits the buffer.
        size_t client_prefix_len = strlen(client_prefix);
        size_t total_len = client_prefix_len + input_len + 2; // +2 for space and newline.

        if (total_len >= CLIENT_BUFFER_SIZE) {
            fprintf(stderr, "ERROR: Message too long\n");
            break;
        }

        // Construct the outgoing message safely.
        char send_buf[CLIENT_BUFFER_SIZE];
        snprintf(send_buf, sizeof(send_buf), "%s %.*s\n",
         client_prefix,
         (int)(CLIENT_BUFFER_SIZE - strlen(client_prefix) - 2),  // maximum number of chars allowed from input
         input);

        // Send the message to the server.
        if (send(sockfd, send_buf, strlen(send_buf), 0) < 0) {
            perror("send");
            break;
        }
    }

    // When input ends (CTRL+D) or an error occurs, close the socket.
    close(sockfd);

    // Cancel and join the receiver thread so that resources are cleaned up.
    pthread_cancel(recv_tid);
    pthread_join(recv_tid, NULL);
    return 0;
}