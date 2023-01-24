/*
2 ** listener.c -- a datagram sockets "server" demo
3 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "utils.h"
#include <pthread.h>
#include <stdbool.h>

#define MAXLINE 1024

int main(void)
{
    bool isClientOrServer = 0; // 1 if client 0 if server
    pthread_t receiverThread, senderThread;

    memset(waitingACKs, -1, 100);
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init has failed\n");
        return 1;
    }

    pthread_create(&receiverThread, NULL, threceive, &isClientOrServer);
    pthread_create(&senderThread, NULL, thsend, &isClientOrServer);
    // pthread_create(&timerThread, NULL, timerfun, NULL);

    pthread_join(senderThread, NULL);
    pthread_join(receiverThread, NULL);
    //pthread_join(timerThread, NULL);

    pthread_mutex_destroy(&lock);

    return 1;
}