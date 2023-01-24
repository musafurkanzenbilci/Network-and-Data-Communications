
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
#include <stdbool.h>
#include <pthread.h>

#define MAXMSGSIZE 12
#define SERVERPORT "4950" // the port users will be connecting to
#define MAXLINE 1024
#define MAXPKGNO 500      //
#define CLIENTPORT "4951" // the port users will be connecting to
#define SERVERIP "172.24.0.10"
#define CLIENTIP "172.24.0.20"
#define TIMEOUT 1000

#define MAXBUFLEN 100


//Packet Struct
struct packet
{
    char message[MAXMSGSIZE];
    char seq_no;
    char msgno; // if -2, it means it is an ACK
    char msglength;
    char msgid;
};



int waitingACKs[MAXPKGNO];
struct packet waitingPackets[MAXPKGNO];
int waitAckIndex = 0;
int waitAckCounter = 0;
int currentTimer = 0;
int seqNoOnTimer;
bool shouldStartTimer = false;
bool endTimer = false;
bool isTimerOn = false;
char *senderPort = CLIENTPORT;
char *senderIP = CLIENTIP;
pthread_mutex_t lock;
pthread_t timerThread;


//Packet Linked List Node
struct pcknode
{
    struct packet pkg;
    struct pcknode *next;
};

/*
Free the memory used by a linked list
*/

void freeList(struct pcknode *head)
{
    struct pcknode *tmp;

    while (head != NULL)
    {
        tmp = head;
        head = head->next;
        free(tmp);
    }
}

/*
Parse the buffer to different packages and put them into a linked list
Return the number of packages
*/
int prepare_packages(char *buffer, int *seq, struct pcknode *packages, int msgid)
{
    //Put \n at the end of the line
    int len = strlen(buffer);
    memset(buffer + len, '\n', 1);
    memset(buffer + len + 1, '\0', 1);

    int current = 0, pckglen = 0, sequence = 0;
    struct pcknode *currNode;
    currNode = packages;
    sequence = *seq;
    bool lastPacket = false;

    while (current < len)//Split into packages
    {
        int msglen = MAXMSGSIZE;
        struct packet pck;
        memset(&pck, '\0', 16);
        pck.seq_no = sequence;
        pck.msgid = msgid;
        pck.msgno = pckglen;
        if (strlen(buffer) < MAXMSGSIZE)
        {
            msglen = strlen(buffer);
            lastPacket = true;
        }
        strncpy(pck.message, buffer, msglen);

        buffer = buffer + msglen;

        currNode->pkg = pck;

        if (!lastPacket)
        { // if equals it is not the last package to be created
            currNode->next = (struct pcknode *)malloc(sizeof(struct pcknode));
            currNode = currNode->next;
            memset(currNode, '\0', sizeof(struct pcknode));
        }
        else
        {
            currNode->next = NULL;
        }

        pckglen++;
        sequence++;
        current += msglen;
    }

    currNode = packages;
    while (currNode)
    {
        currNode->pkg.msglength = pckglen;
        currNode = currNode->next;
    }

    return pckglen;
}

/*
Add into a queue
*/
void add_queue(struct packet pkg, struct packet *queue, int *tail)
{
    queue[*tail] = pkg;
    *tail += 1;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

//Last step send function used in general
void usend(struct packet pkg, int sockfd, struct addrinfo *p)
{
    int numbytes;//Used for checking how many byte is sent
    
    if ((numbytes = sendto(sockfd, &pkg, sizeof(pkg), 0,
                           p->ai_addr, p->ai_addrlen)) == -1)
    {
        perror("talker: sendto");
        exit(1);
    }
}

//Timeout Protocol
void sendPackagesOnTimeout(char *ipaddr, char *port, int sendFrom)
{
    int sockfd;

    // SOCKET CREATION
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_in servaddr;

    // QUEUE
    struct packet queue[MAXPKGNO];
    int qtail = 0;
    int window = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    if ((rv = getaddrinfo(ipaddr, port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }

    // loop through all the results and make a socket
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "talker: failed to create socket\n");
        return;
    }

    pthread_mutex_lock(&lock);
    int i = 0;
    
    //Send Waiting Packages on Timeout
    for (; i < waitAckCounter; i++)
    {
        usend(waitingPackets[i], sockfd, p);
        shouldStartTimer = true;
    }
    pthread_mutex_unlock(&lock);
    freeaddrinfo(servinfo);

    close(sockfd);

    return;
}

void startTimer()
{
    int milliseconds = TIMEOUT;

    // a current time of milliseconds
    currentTimer = clock() * 1000 / CLOCKS_PER_SEC;

    // needed count milliseconds of return from this timeout
    int end = currentTimer + milliseconds;
    printf("Timer Started with current Time: %d\n", currentTimer);
    // wait while until needed time comes
    do
    {
        currentTimer = clock() * 1000 / CLOCKS_PER_SEC;

    } while (currentTimer <= end && !endTimer);

    if (!endTimer)//It means time out
    {
        pthread_mutex_lock(&lock);
        int sendfrom = seqNoOnTimer;
        pthread_mutex_unlock(&lock);
        sendPackagesOnTimeout(senderIP, senderPort, sendfrom);
    }
    else
    {//Timer is ended by ACK
    
        endTimer = false;
        // if there are more waiting ACK, start timer again
        pthread_mutex_lock(&lock); // Unlock mutex for shared resources
        int ackCounter = waitAckCounter;
        pthread_mutex_unlock(&lock); // Unlock mutex for shared resources

        if (ackCounter > 1)
        {
            printf("Start Again Counter: %d\n", ackCounter);
            startTimer();
        }
    }
}

/*
Timer Thread Function
*/
void *timerfun(void *vargp)
{
    if (!isTimerOn && shouldStartTimer)
    {
        isTimerOn = true;
        startTimer();
        isTimerOn = false;
        endTimer = false;
    }
    return NULL;
}


/*
Send Thread Function Used by Both Sides
Based on Beej's Guide
*/
void *thsend(void *vargp)
{
    bool *b = (bool *)vargp;
    bool isClientOrServer = *b;
    // struct packet pkg;
    int newLineCounter = 0;
    int sequenceNo = 0;
    int sockfd;
    char buffer[MAXLINE];

    // SOCKET CREATION
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_in servaddr;

    // PREPARE PACKAGE
    struct pcknode *pkgs, *looppkg;
    int msgid = 0;
    pkgs = (struct pcknode *)malloc(sizeof(struct pcknode));
    memset(pkgs, '\0', sizeof(struct pcknode));
    memset(waitingPackets, '\0', MAXMSGSIZE * sizeof(struct packet));

    // QUEUE
    struct packet queue[MAXPKGNO];
    int qtail = 0;
    int window = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    if (isClientOrServer)
    {
        senderPort = SERVERPORT;
        senderIP = SERVERIP;
    }

    if ((rv = getaddrinfo(senderIP, senderPort, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL;
    }

    // loop through all the results and make a socket
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "talker: failed to create socket\n");
        return NULL;
    }

    while (1)
    {
        memset(buffer, '\0', MAXLINE);

        scanf("%[^\n]%*c", buffer);//Get the input

        if (strlen(buffer) == 0) //Check Termination Condition
        { // if new line, increment and check counter
            if (++newLineCounter > 2)
            {
                exit(1);
            }
            continue;
        }

        int num = prepare_packages(buffer, &sequenceNo, pkgs, msgid);
        msgid++;
        sequenceNo = num;
        //Loop through prepared packages for the line
        for (looppkg = pkgs; looppkg != NULL; looppkg = looppkg->next)
        {
            struct packet pkg = looppkg->pkg;
            // we should add the packages into a queue
            add_queue(pkg, queue, &qtail);

            usend(pkg, sockfd, p);

            pthread_mutex_lock(&lock); 
            //Add sent packets into waitingACKs
            waitingACKs[waitAckCounter] = pkg.seq_no;
            waitingPackets[waitAckCounter] = pkg;
            waitAckCounter++;
            pthread_mutex_unlock(&lock);

            if (!isTimerOn)
            {
                shouldStartTimer = true;
                seqNoOnTimer = pkg.seq_no;

                //IF I UNCOMMENT THİS TİMER WILL START
                //pthread_create(&timerThread, NULL, timerfun, NULL);
                //pthread_join(timerThread, NULL);
            }
        }
    }

    freeList(pkgs); // free prepare_packages linked list
    freeaddrinfo(servinfo);

    close(sockfd);

    return NULL;
}


/*
Function Used for Sending ACK in threceive fun
*/
void sendACKonReturn(char *ipaddr, char *port, struct packet pkg)
{ 

    int sockfd;

    // SOCKET CREATION
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_in servaddr;

    // QUEUE
    struct packet queue[MAXPKGNO];
    int qtail = 0;
    int window = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    if ((rv = getaddrinfo(ipaddr, port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }

    // loop through all the results and make a socket
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "talker: failed to create socket\n");
        return;
    }

    // Send packet
    usend(pkg, sockfd, p);

    freeaddrinfo(servinfo);

    close(sockfd);

    return;
}


/*
Listen Thread Function Used by Both Sides
Based on Beej's Guide
*/
void *threceive(void *vargp)
{
    bool *b = (bool *)vargp;
    bool isClientOrServer = *b;
    int sockfd;
    char *port = SERVERPORT;
    char *sendIP = CLIENTIP;
    char *sendPort = CLIENTPORT;
    struct addrinfo hints, *servinfo, *p;
    char buffer[MAXLINE];
    struct packet messageq[120];
    int qindex = 0;
    struct packet rcvpkg[16];
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
    char s[INET_ADDRSTRLEN];
    int waitingAckNo = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if (isClientOrServer)
    {
        port = CLIENTPORT;
        sendIP = SERVERIP;
        sendPort = SERVERPORT;
    }

    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "listener: failed to bind socket\n");
        return NULL;
    }

    while (1)
    {
        memset(rcvpkg,'\0', 16);
        addr_len = sizeof their_addr;

        //Wait for Receiving a Package
        if ((numbytes = recvfrom(sockfd, rcvpkg, 16, 0,
                                 (struct sockaddr *)&their_addr, &addr_len)) == -1)
        {
            perror("recvfrom");
            exit(1);
        }
        
        if (rcvpkg->msgno == -2) // if it is an ACK
        {
            if (rcvpkg->seq_no != waitingAckNo)
            {
                continue;
            }

            pthread_mutex_lock(&lock); // Unlock mutex for shared resources
            int i = 0;


            for (; i < waitAckCounter; i++)
            {
                if (waitingACKs[i] == rcvpkg->seq_no)
                { // if there is waiting ack with matched seq no
                    int j = 0;
                    for (; j < waitAckCounter; j++)
                    {
                        //CUMULATIVE ACK
                        if (waitingACKs[j] < rcvpkg->seq_no && waitingACKs[j] > -1)
                        {
                            // softdelete from list
                            int k = j;
                            //shift the elements in the list
                            for (; k < waitAckCounter; k++)
                            {
                                waitingACKs[k] = waitingACKs[k + 1];
                                waitingPackets[k] = waitingPackets[k + 1];
                            }
                            waitAckCounter--;
                        }
                    }
                    // end timer
                    endTimer = true;
                    // printf("endTimer order is given\n");
                    break;
                }
            }

            pthread_mutex_unlock(&lock);
        }
        else // If Standart Package
        { 
            add_queue(*rcvpkg, messageq, &qindex);

            printf("%s", rcvpkg->message); //MSG OUTPUT last packet contains \n

            // After receiving packet, we should send an ACK message back
            struct packet ACKpckg;
            memset(&ACKpckg, '\0', 16);
            ACKpckg = *rcvpkg;
            ACKpckg.seq_no = waitingAckNo;
            ACKpckg.msgno = -2;
            sendACKonReturn(sendIP, sendPort, ACKpckg);//send ACK package
            waitingAckNo++;
        }
    }

    freeaddrinfo(servinfo);

    close(sockfd);

    return NULL;
}