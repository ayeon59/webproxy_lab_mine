#include "csapp.h"


/*main parameter definition
argc : QTY of arguments delivered by instruction(By default minimum == 1 because of program's own name)
argv : argument vector, an array of strings containing the arguments passed to the program
*/
int main(int argc, char **argv){
    /*최종적으로 클라이언트 측에서 사용할 소켓 fd*/
    int clientfd;
    /*declare variable for host and port number */
    /* MAXLINE : maximum size that can be handled at once
    헤더 파일에 "MAXLINE  8192"로 정의되어 있기 때문에 문자열을 다룰 때
    최대 8192byte 까지는 한번에 다룰 수 있다.
     */
    char *host, *port, buf[MAXLINE];
    /*declare pointer of rio struct
    버퍼링된 I/O를 구현하기 위해 필요한 상태 정보를 담고 있는 구조체이고,
    일반적인 read/write 시스템호출의 단점을 줄이기 위한 방법이다.
    */
    rio_t rio;
    /*To inspect QTY of parameter */
    /*argc must have 3 arguments at least.
    because it should have arguments for program name, domain(host) and port number*/
    if (argc != 3){
        /*Warning : you must enter the correct input values */
        fprintf(stderr,"usage: %s <host> <port>\n",argv[0]);
        /*Terminate forcibly and the user must run it again with correct input value*/
        exit(0);
    }
    /*Set host and port*/
    host = argv[1];
    port = argv[2];

    /*clientfd is a socket fd that manages communication 
    with the server used by the client
     */
    /* Open_clientfd : establishes a connection to the server
   and returns a socket file descriptor for communication */
    clientfd = Open_clientfd(host,port);
    /*Initialize rio structure for clienftd*/
    Rio_readinitb(&rio,clientfd);

    /*Fgets : Reads one line of text input from the user and stores it into the buffer*/
    while (Fgets(buf,MAXLINE,stdin) != NULL) {
        /*Rio_written : Send bytes in buffer to client fd*/
        Rio_writen(clientfd,buf,strlen(buf));
        /* Data written to the client socket is transmitted over the network,
        delivered to the server, and the server's response is sent back
        into the socket's receive buffer by the kernel/TCP stack. */

        /*Rio_readlineb : read(=input) a line of bytes from the server into buf */
        Rio_readlineb(&rio,buf,MAXLINE);
        /*write the string in buf to the user's terminal*/
        Fputs(buf, stdout);
    }
    /* Close : Terminate the connection and release the client socket descriptor.
    This sends a FIN to the server and frees related resources. */
    Close(clientfd);
    exit(0);

}