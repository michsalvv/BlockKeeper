#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define CLEAR_SCREEN "\033[2J\033[H"
#define RESET       "\033[0m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"

#define PUT 134
#define GET 156
#define INVALIDATE 174

void action1() {
    int block, ret, size;
    char buffer[4096];

    printf("%sEnter the block ID: %s", GREEN, RESET);
    scanf("%d", &block);
    printf("Bytes to read: ");
    scanf("%d", &size);

    printf("Calling GET syscall on block %d\n", block);
    ret = syscall(GET, block, &buffer, size);
    printf("Result: %d\n",ret);

    if (ret >0)
        printf("Buffer: \n%s", buffer);

    if (ret == -1 && errno == ENODATA)
        printf("Returned: %d\n", errno);
}

void action2() {
    printf("%sInvalidate block number: %s", GREEN, RESET);
    int value;
    scanf("%d", &value);
}

void action3() {
    printf("%sAction 3 selected.%s\n", GREEN, RESET);
    int value;
    printf("Enter an integer value: ");
    scanf("%d", &value);
    printf("You entered: %d\n", value);
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

            
        printf("Readed bytes: %d\n", ret);
        printf("Returned buffer>\n%.*s\n", ret, buffer);

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
                action1();
                break;
            case 2:
                action2();
                break;
            case 3:
                action3();
                break;
            case 4:
                vfs_read();
                break;
            case 5:
                printf("%sExiting...%s\n", RED, RESET);
                break;
            default:
                printf("%sInput not valid!%s\n", RED, RESET);
                break;
        }

        printf("\nPress Enter to continue...");
        while (getchar() != '\n') {}
        getchar();
    } while (choice != 5);

    return 0;
}
