#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "builtins.h"
#include "variables.h"
#include "io_helpers.h"
#define MAX_EXPANDED_LEN 128  // Maximum allowed length after expansion

void execute_pipe(char **cmd1, char **cmd2);
int start_background_process(char **cmd);
int execute_system_command(char **cmd);


int is_alnum_or_underscore(char c) {
    if ((c >= 'A' && c <= 'Z') ||  // Uppercase letters
        (c >= 'a' && c <= 'z') ||  // Lowercase letters
        (c >= '0' && c <= '9') ||  // Digits
        (c == '_')) {              // Underscore
        return 1;  // Character is alphanumeric or underscore
    }
    return 0;  // Character is not alphanumeric or underscore
}

void sigint_handler(int signum) {
    (void)signum; // Suppress unused parameter warning
    // Print a newline and reprint the prompt:
    write(STDOUT_FILENO, "\nmysh$ ", 7);
}


// You can remove __attribute__((unused)) once argc and argv are used.
int main(__attribute__((unused)) int argc, 
         __attribute__((unused)) char* argv[]) {
    char *prompt = "mysh$ ";

    char input_buf[MAX_STR_LEN + 1];
    input_buf[MAX_STR_LEN] = '\0';
    char *token_arr[MAX_STR_LEN] = {NULL};

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    sa.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }



    while (1) {
        // Prompt and input tokenization

        // Display the prompt via the display_message function.
	display_message(prompt);

        int ret = get_input(input_buf);
        size_t token_count = tokenize_input(input_buf, token_arr);

        // Clean exit
        if (ret != -1 && (token_count == 0 || (strcmp("exit", token_arr[0]) == 0))) {
		break;
        }

    	// Check for a pipe in the command
    	int pipe_index = -1;
    	for (size_t i = 0; i < token_count; i++) {
        	if (strcmp(token_arr[i], "|") == 0) {
            		pipe_index = i;
            		break;
        	}
    	}

	if (token_count >= 1 && pipe_index == -1) {
    		// Check for variable assignment (e.g., myvar=hello)
    		char *equal_sign = strchr(token_arr[0], '=');

    		if (equal_sign != NULL) {
        		*equal_sign = '\0'; // Split key and value
	 		char *key = token_arr[0];
        		char *value = equal_sign + 1;
		
		if (value[0] == '$'){
			const char *variable_key = value + 1;
			const char *variable_value = get_variable(variable_key);
			value = (char *)variable_value;
		}
	

        	set_variable(key, value);
        	continue; // Skip command execution
    		}	
	}

	// Expand variables in command arguments

	int was_allocated[token_count];  // Track dynamically allocated strings
	memset(was_allocated, 0, sizeof(was_allocated));
	int total_expaned_len = 0; 

	for (size_t i = 0; i < token_count; i++) {
    		char *orig = token_arr[i];

    		if (orig[0] == '$') {
        		char expanded[MAX_EXPANDED_LEN] = "";
        		size_t expanded_len = 0;
        		size_t orig_len = strlen(orig);

        		for (size_t j = 0; j < orig_len;) {
            			if (orig[j] == '$' && strlen(orig) == 1) {
                			expanded[1] = '$';
                			total_expaned_len++;
            			}

            			if (orig[j] == '$' && strlen(orig) != 1) {
                			j++; // Skip the '$'
                			char var_name[MAX_STR_LEN] = "";
                			size_t var_len = 0;

                			// Extract the variable name
                			while (j < orig_len && is_alnum_or_underscore(orig[j])) {
                    				var_name[var_len++] = orig[j++];
                			}

                			var_name[var_len] = '\0';
                			const char *var_value = get_variable(var_name);

                			if (var_value) {
                    				// Append the variable value to expanded
                    				var_len = strlen(var_value);
                    				size_t remaining_space = MAX_EXPANDED_LEN - total_expaned_len;

                    				if (remaining_space > 0) {
                        				size_t copy_len = (var_len < remaining_space) ? var_len : remaining_space;
                        				strncat(expanded, var_value, copy_len);
                        				total_expaned_len += copy_len;
                    				}
                			}

            			} else {
                			// Append the current character to expanded
                			char temp[2] = {orig[j++], '\0'};
                			strncat(expanded, temp, MAX_EXPANDED_LEN - expanded_len - 1);
                			expanded_len++;
            			}
							
        		}
        	token_arr[i] = strdup(expanded);
        	was_allocated[i] = 1;
    		}
	}
	
	int suc = -1;
	if (token_count >= 1) {
		if (token_count >= 1 && strcmp(token_arr[token_count - 1], "&") == 0) {
    			// Background process detected
   			token_arr[token_count - 1] = NULL; // Remove '&' from the arguments
    			suc = start_background_process(token_arr);
		}

    		if (pipe_index != -1) {
        		// Handle pipe execution
        		token_arr[pipe_index] = NULL; // Split the command into two parts

        		// First command: tokens before the pipe
        		char *cmd1[pipe_index + 1];
        		for (int i = 0; i < pipe_index; i++) {
            			cmd1[i] = token_arr[i];
        		}			
        		cmd1[pipe_index] = NULL;

        		// Second command: tokens after the pipe
        		char *cmd2[token_count - pipe_index];
        		for (size_t i = pipe_index + 1, j = 0; i < token_count; i++, j++) {
            			cmd2[j] = token_arr[i];
        		}
        		cmd2[token_count - pipe_index - 1] = NULL;

        		// Execute the two commands with a pipe
        		execute_pipe(cmd1, cmd2);
    	} else {
        	//check for a built-in function
        	bn_ptr builtin_fn = check_builtin(token_arr[0]);
        	if (builtin_fn != NULL) {
            		ssize_t err = builtin_fn(token_arr);
            	if (err == -1) {
                	display_error("ERROR: Builtin failed: ", token_arr[0]);
            	}
        	} else if (builtin_fn == NULL && suc == -1){
            		if (execute_system_command(token_arr) == -1) {
        		display_error("ERROR: Unknown command: ", token_arr[0]);
        		}
		}
    	}
}


	for (size_t i = 0; i < token_count; i++) {
    		if (was_allocated[i]) {
        		free(token_arr[i]);
    		}
	}
}
    free_variables();

    return 0;
}
