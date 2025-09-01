#include "csapp.h"
/*connfd : specific socket fd to connect client with werver*/
void echo(int connfd){
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    /*init by connecting rio buffer and connfd*/
    Rio_readinitb(&rio,connfd);
    
    /*the call to Rio_readlineb blocks until the client sends some data.
    - If the client sends data → n > 0 → the data is copied into buf and the loop body executes.
	- If the client sends nothing → the function just waits (blocks).
	- If the client closes the connection (EOF) → n == 0 → the loop ends
    */
    while((n = Rio_readlineb(&rio,buf,MAXLINE)) != 0 ){
        /*to notify something is comming to server from client*/
        printf("server received %d bytes\n",(int)n);
        /*Echo the same data that comes to server into client*/
        Rio_writen(connfd,buf,n);
    }

}