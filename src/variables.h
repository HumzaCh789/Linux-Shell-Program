#ifndef VARIABLES_H
#define VARIABLES_H

typedef struct VarNode {
    char *name;
    char *value;
    struct VarNode *next;
} VarNode;

// Function prototypes
void set_variable(const char *name, const char *value);
const char *get_variable(const char *name);
void delete_variable(const char *name);
void free_variables(void);

#endif // VARIABLES_H


