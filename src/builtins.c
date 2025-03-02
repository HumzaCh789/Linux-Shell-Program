#include <string.h>
#include "builtins.h"
#include "io_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
 


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
    // Check if a file was provided
    if (tokens[1] == NULL) {
        display_error("ERROR: No input source provided", "");
        return -1;
    }
    
    // Open the file for reading
    FILE *fp = fopen(tokens[1], "r");
    if (fp == NULL) {
        display_error("ERROR: Cannot open file: ", tokens[1]);
        return -1;
    }
    
    // Read and display the file line by line.
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        display_message(line);
    }
    
    fclose(fp);
    return 0;
}

ssize_t bn_wc(char **tokens) {
    // Check if file argument is provided
    if (tokens[1] == NULL) {
        display_error("ERROR: No input source provided", "");
        return -1;
    }
    
    FILE *fp = fopen(tokens[1], "r");
    if (fp == NULL) {
        display_error("ERROR: Cannot open file: ", tokens[1]);
        return -1;
    }
    
    unsigned long word_count = 0;
    unsigned long char_count = 0;
    unsigned long newline_count = 0;
    int in_word = 0;
    int c;
    
    // Read file character by character
    while ((c = fgetc(fp)) != EOF) {
        char_count++;  // Count every character
        
        if (c == '\n') {
            newline_count++;
        }
        
        // Determine if the character is whitespace.
        // We consider: space, newline, tab, and carriage return as whitespace.
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            in_word = 0;
        } else {
            // If we transition from whitespace to a non-whitespace character, count a new word.
            if (!in_word) {
                word_count++;
                in_word = 1;
            }
        }
    }
    
    fclose(fp);
    
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