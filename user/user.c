#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define PUT 134
#define GET 156
#define INVALIDATE 174

int main(int argc, char **argv){
    int ret, pid, i=0;
    char buffer[4096];

    // pid = fork();
    // if (pid == 0){
    //     // sleep(2);
    //     ret = syscall(INVALIDATE, 1);
    //     printf("[Thread] Returned %d\n", ret);
    //     return 0;
    // }

    ret = syscall(PUT, "testo", 5);
    if (ret<0){
        printf("Errno: %d\n", errno);
        return -1;
    }
    printf("Returned %d\n", ret);

    // // while(1){
    //     for (i=1; i<12; i++){
    //         printf("Get on %d block\n", i);
        // ret = syscall(GET, 1, &buffer, 26);
        // printf("Returned %d\nBuffer: %s", ret, buffer);
        
    //     }
    // //     if (ret <0) break;
    // // }

    // int fd; 
    
    // fd = open("mount/the-file", O_RDWR);
    // if (!fd){
    //     printf("open error\n");
    //     return -1;
    // }
    // ret = read(fd, &buffer, 100);
    // printf("Readed %d bytes\n----\n%s", ret, buffer);

    return 0;
}
