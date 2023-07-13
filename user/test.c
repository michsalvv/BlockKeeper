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

#define MAX_MSG_SIZE 300 //TODO change (4kb-metadata)

void get_action() {
    int block, ret, size;
    char buffer[4096];

    printf("%sEnter the block ID: %s", GREEN, RESET);
    scanf("%d", &block);
    printf("Bytes to read: ");
    scanf("%d", &size);

    ret = syscall(GET, block, &buffer, size);

    if (ret >0){
        printf("Readed [%d] bytes:\n\n------------------", ret);
        printf("%s\n%s\n%s",BLUE, buffer, RESET);
        printf("------------------\n");
    }

    if (ret == -1 && errno == ENODATA)
        printf("Returned: %d\n", errno);
}

void inv_action() {
    int block, ret;
    printf("%sInvalidate block number: %s", GREEN, RESET);
    scanf("%d", &block);

    printf("Calling INVALIDATE syscall on block %d\n", block);
    ret = syscall(INVALIDATE, block);
    printf("Result: %d\n",ret);
}

void put_action() {
    char user_buf[MAX_MSG_SIZE];
    int ret;

    printf("%sMessage to save: %s", GREEN, RESET);
    scanf(" %[^\n]", user_buf);

    ret = syscall(PUT, user_buf, strlen(user_buf));
    printf("Returned %d\n", ret);

}

void vfs_read(){
    int size, ret;
    char buffer[4096];
    
    int fd = open("mount/the-file", O_RDONLY);
    if (fd == -1) {
        printf("Error occurred while opening file");
        return;
    }

    do {
        
        printf("Bytes to read [ 0 to exit ]: ");
        scanf("%d", &size);
        if (size == 0){
            break;
        }
            
        ret = read(fd, buffer, size);
        if (ret >=0){
            printf("Readed bytes: %d\n", ret);
            if (ret >0 ) printf("Returned buffer>\n%.*s\n", ret, buffer);
        }

    } while (1);
    close(fd);

    return;

}

int main() {
    int choice;

    do {
        printf(CLEAR_SCREEN);
        printf("%sBlockKeeper User Interface:%s\n", BLUE, RESET);
        printf("1. Get a Block\n");
        printf("2. Invalidate a Block\n");
        printf("3. Put a Block\n");
        printf("4. Read via VFS\n");
        printf("5. Exit\n");

        printf("\n%sChoose: %s", YELLOW, RESET);
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                get_action();
                break;
            case 2:
                inv_action();
                break;
            case 3:
                put_action();
                break;
            case 4:
                vfs_read();
                break;
            case 5:
                printf("%sExiting...%s\n", RED, RESET);
                return 0;
            default:
                printf("%sInput not valid!%s\n", RED, RESET);
                break;
        }

        printf("\nPress Enter to continue...");
        while (getchar() != '\n') {}
        getchar();
    } while (1);

    return 0;
}
