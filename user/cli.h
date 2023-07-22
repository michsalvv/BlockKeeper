#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#define CLEAR_SCREEN "\033[2J\033[H"
#define RESET       "\033[0m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"

#define PUT 134
#define GET 156
#define INVALIDATE 174

//sizeof(metadata) = 11
#define MAX_MSG_SIZE 4096 - 11

#define derror(...) do { \
    printf("%s", RED); \
    printf(__VA_ARGS__); \
    printf("%s", RESET); \
} while (0)



// Error handlers definitions
extern void inv_handler(int errno_code, int blockID);
extern void get_handler(int errno_code, int blockID);
extern void put_handler(int errno_code);