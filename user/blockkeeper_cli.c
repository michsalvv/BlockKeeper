#include "cli.h"

/**********************************************************
 * User CLI
 **********************************************************/

void get_action() {
    int block, ret, size;
    char buffer[MAX_MSG_SIZE] = "";
    
    printf("%sEnter the block ID: ", YELLOW);
    scanf("%d", &block);
    printf("Bytes to read: %s", RESET);
    scanf("%d", &size);

    ret = syscall(GET, block, &buffer, size);

    if (ret >=0){
        printf("\nReaded [%s%d%s] bytes:\n------------------",GREEN, ret, RESET);
        printf("%s\n%s%s",BLUE, buffer, RESET);
        printf("\n------------------\n");
        goto ret;
    }

    get_handler(errno, block);

ret:
    fflush(stdout);
    return;
}

void inv_action() {
    int block, ret;
    printf("%sInvalidate block number: %s", YELLOW, RESET);
    scanf("%d", &block);
    ret = syscall(INVALIDATE, block);

    if (ret==0){
        printf("%sBlock %d has been invalidated%s\n",GREEN, block, RESET );
        goto ret;
    }

    inv_handler(errno, block);

ret:
    fflush(stdout);
    return;
}

void put_action() {
    int ret;
    char user_buf[MAX_MSG_SIZE - 1]; // Reserve one byte for null character
    char format_string[20];
    sprintf(format_string, " %%%d[^\n]", MAX_MSG_SIZE);

    printf("%sMessage to save: %s", YELLOW, RESET);
    scanf(format_string, user_buf);

    ret = syscall(PUT, user_buf, strlen(user_buf));
    if (ret >=0){
        printf("\n> Your message has been saved in block %s[%d]%s\n", GREEN, ret, RESET);
        goto ret;
    }

    put_handler(errno);

ret:
    fflush(stdout);
    return;
}

void vfs_read(){
    int size, ret;
    char buffer[MAX_MSG_SIZE];
    
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
            printf("Readed [%s%d%s] bytes:\n\n",GREEN, ret, RESET);
            if (ret >0 ) printf("%.*s\n", ret, buffer);
        }else if(errno == ESPIPE){
            printf("%sThe block you were reading has been invalidated. Reset the file descriptor%s\n", RED, RESET);
            break;
        }

    } while (1);
    close(fd);

    return;

}

int main() {
    int choice;
//    vfs_read();
//    return 0;

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
