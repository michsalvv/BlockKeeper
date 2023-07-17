#include "cli.h"

#include <pthread.h>
#include <time.h>

#define NUM_THREADS 3 // One thread for each task

#ifndef NUM_BLOCKS
    #define NUM_BLOCKS 0
#endif

void* getBlock(void* arg) {
    int blockID, ret, t_id, cycles;
    char buffer[MAX_MSG_SIZE];

    t_id = *(int *)arg;
    cycles = 5;

    while(1){
        for (blockID = 0; blockID < NUM_BLOCKS - 2; blockID++){
            memset(buffer, 0, MAX_MSG_SIZE);

            ret = syscall(GET, blockID, &buffer, 100);
            printf("[Thread #%d | Reading Block %d] - ", t_id, blockID);
            if (ret >= 0){
                printf("%s", buffer);
            }else get_handler(errno, blockID);
            sleep(1.5);
        }
    }


    pthread_exit(NULL);
}

void* invBlock(void* arg){
    int blockID, ret, t_id;
    t_id = *(int*)arg;

    srand(time(NULL));
  
    while(1){
        blockID = rand() % NUM_BLOCKS;
        ret = syscall(INVALIDATE, blockID);

        printf("[Thread #%d | Invalidating Block %d] ", t_id, blockID);
        if (ret == 0){
            printf("Block %d invalidated\n",blockID);
        }else inv_handler(errno, blockID);
        sleep(3);
    }
    
    pthread_exit(NULL);
}

void* putBlock(void* arg){
    int ret, t_id, count = 0;
    char user_msg[100];
    t_id = *(int*)arg;

    while (1){
        sprintf(user_msg, "Text saved from %d-th put operation by the testing thread", count);
        ret = syscall(PUT, user_msg, strlen(user_msg));
        printf("[Thread #%d | Put] ", t_id); 
        if (ret >= 0){
            printf("Message saved in block %d\n",ret);
            count++;
        }else put_handler(errno);

        sleep(1);
    }

    pthread_exit(NULL);
    
}

void spawn(void* func, pthread_t *thread, void* threadArgs){
    int ret = pthread_create(thread, NULL, func, threadArgs);
    if (ret != 0){
        fprintf(stderr, "Failed to create thread\n");
        exit(1);
    }
}

int main() {
    pthread_t threads[NUM_THREADS];
    int threadArgs[NUM_THREADS];
    int thread_nr, blockID;

    if (NUM_BLOCKS == 0){
        derror("Not enough blocks. To run the tests, use the instructions in the README: define N_BLOCKS variable in Makefile.");
        return -1;
    }

    srand(time(NULL));
    blockID = rand() % NUM_BLOCKS;

    for (thread_nr = 0; thread_nr < NUM_THREADS; thread_nr++) {
        threadArgs[thread_nr] = thread_nr;

        switch (thread_nr){
        case 0:
            spawn(getBlock, &threads[thread_nr], &threadArgs[thread_nr]);
            break;
        
        case 1:
            spawn(invBlock, &threads[thread_nr], &threadArgs[thread_nr]);
            break;

        case 2:
            spawn(putBlock, &threads[thread_nr], &threadArgs[thread_nr]);
            break;

        default:
            break;
        }

    }

    // Join dei thread
    for (thread_nr = 0; thread_nr < NUM_THREADS; thread_nr++) {
        int result = pthread_join(threads[thread_nr], NULL);
        if (result != 0) {
            fprintf(stderr, "Failed to join thread %d: %d\n", thread_nr, result);
            exit(1);
        }
    }

    printf("All threads have exited. Exiting main thread.\n");

    return 0;
}
