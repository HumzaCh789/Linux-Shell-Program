#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define MAX_VALUE_LENGTH 128

// Structure for a variable node
typedef struct VarNode {
    char *key;
    char *value;
    struct VarNode *next;
} VarNode;

VarNode *head = NULL; // Head of the linked list

// Function to insert or update a variable
void set_variable(const char *key, const char *value) {
    VarNode *current = head;

    // Check if the variable already exists
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            // Update existing variable
            free(current->value);
            current->value = strdup(value);
            if (current->value == NULL) {
                // Handle memory allocation failure for value
                const char *error_msg = "ERROR: Memory allocation failed for value\n";
                write(STDERR_FILENO, error_msg, strlen(error_msg));
                return;
            }
            return;
        }
        current = current->next;
    }

    // Create a new variable node
    VarNode *new_node = (VarNode *)malloc(sizeof(VarNode));
    if (new_node == NULL) {
        // Handle memory allocation failure for new node
        const char *error_msg = "ERROR: Memory allocation failed for new variable node\n";
        write(STDERR_FILENO, error_msg, strlen(error_msg));
        return;
    }

    new_node->key = strdup(key);
    if (new_node->key == NULL) {
        // Handle memory allocation failure for key
        free(new_node);
        const char *error_msg = "ERROR: Memory allocation failed for key of variable\n";
        write(STDERR_FILENO, error_msg, strlen(error_msg));
        return;
    }

    new_node->value = strdup(value);
    if (new_node->value == NULL) {
        // Handle memory allocation failure for value
        free(new_node->key);
        free(new_node);
        const char *error_msg = "ERROR: Memory allocation failed for value of variable\n";
        write(STDERR_FILENO, error_msg, strlen(error_msg));
        return;
    }

    new_node->next = head;
    head = new_node;
}



// Function to get a variable's value
const char *get_variable(const char *key) {
    VarNode *current = head;
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            return current->value;
        }
        current = current->next;
    }
    return ""; // Undefined variables return an empty string
}

// Function to free all memory on exit
void free_variables() {
    VarNode *current = head;
    while (current != NULL) {
        VarNode *temp = current;
        current = current->next;
        
        // Free key, value, and node itself
        free(temp->key);
        free(temp->value);
        free(temp);
    }
    head = NULL;  //  Set head to NULL to avoid dangling pointer issues
}
