#include "csapp.h"
#include "echo.c"

void echo(int connfd);

int main(int argc, char **argv){
    /* listenfd : eventually becomes the listening socket (used only to accept new client connections)
    connfd  : connected socket (used to communicate with a specific client) */
    int listenfd, connfd;
    /*clientlen : Length of clientarr*/
    socklen_t clientlen;
    /*clientlen : Length of clientaddrr*/
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE],client_port[MAXLINE];
    
    /*argc must be 2 because it must contain program name and port number*/
    if (argc!=2){
        fprintf(stderr,"usage: %s <port>\n",argv[0]);
        /*if it doesn't have valid input valuee, terminate the program and let the user restart with the correct value*/
        exit(0);
    }
    /* listenfd : The unique server listening socket fd bound to the specific port (argv[1]),
    used to accept connection requests from multiple clients */
    listenfd = Open_listenfd(argv[1]);

    while(1){
        clientlen = sizeof(struct sockaddr_storage);
        /* Accept : takes one pending request from the listening socket and returns 
        a new connection fd (connfd) dedicated to that client */
        connfd = Accept(listenfd,(SA *)&clientaddr,&clientlen);
        /*Translate clientaddr and clientlen into string we can read*/
        Getnameinfo((SA *) &clientaddr,clientlen,client_hostname,MAXLINE,client_port,MAXLINE,0);
        /*print Information of connection */
        printf("Connected to (%s, %s)\n", client_hostname,client_port);
        echo(connfd);
        close(connfd);
    }
    exit(0);
}