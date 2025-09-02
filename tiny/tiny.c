#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename,char *cgiargs);
void serve_static(int fd,char *filename, int filesize);
void get_filetype(char *filename, char* filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);


/*<------------------------------------------------------------------------------------------------------------------->*/

//connect client with serve

int main(int argc, char **argv){
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    
    /*argc must be 2 because it must contain program name and port number*/
    if(argc!=2){
        fprintf(stderr, "usage: %s <port>\n",argv[0]);
        exit(1);
    }
    // create the unique listening socket for the current port number
    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen = sizeof(clientaddr);
        /*extract one pending client connection from listenfd
        and create a new connected socket (connfd) for actual communication*/
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        /*convert the client address into human-readable hostname and port strings*/
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0);
        printf("Accepted connection from (%s, %s)\n", hostname,port);
        doit(connfd);
        Close(connfd);
    }

/*<------------------------------------------------------------------------------------------------------------------->*/    

}
void doit(int fd){
    int is_static;  
    /* Flag to indicate whether the request is for a static file (1) or a dynamic resource (0)*/
    struct stat sbuf;  
    /* Struct for holding file metadata retrieved by the stat() system call*/

    /* Buffers for parsing the HTTP request line*/
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];  
    /*buf     → raw request line from the client  
    method  → HTTP method (e.g., "GET", "HEAD")  
    uri     → requested URI (e.g., "/index.html")  
    version → HTTP version string (e.g., "HTTP/1.0")*/

    // Buffers for processing the request target:  
    char filename[MAXLINE], cgiargs[MAXLINE];  
    /*  
    filename → actual file path on the server (e.g., "./home.html")  
    cgiargs  → arguments for CGI programs (dynamic content)*/

    rio_t rio;  
    /*robust I/O buffer structure used for efficient and safe I/O operations with the client socket*/

    //connect fd with rio buffer
    Rio_readinitb(&rio,fd);
    //read request line 
    /*ex : GET / index.html HTTP/1.1*/
    Rio_readlineb(&rio,buf,MAXLINE);
    /*debug log*/
    printf("Request headers:\n");
    printf("%s", buf);
    /*parse request line into 3 token*/
    /*ex: method = "GET", uri = "/index.html", version="HTTP/1.1" */
    sscanf(buf, "%s %s %s",method,uri,version);

    // if the HTTP method isn't GET types
    if (strcasecmp(method,"GET")){
        //send an error message to the client(e.g., a web browser)
        clienterror(fd,filename,"501","Not implemented","Tiny does not implement this method");
        return ; 
    }
    // To read http request head
    read_requesthdrs(&rio);
    /* The return value is for a static file (1) or a dynamic resource (0)*/
    is_static = parse_uri(uri,filename,cgiargs);

    // Check if the requested file path exists on the server
    if(stat(filename, &sbuf)<0){
        /* If the file does not exist, send an HTTP 404 error response to the client via the socket (fd) */
        clienterror(fd,filename,"404","Not found","Tiny couldn't find this file");
        return;
    }

    // Handle static content
    if (is_static){
        /* Must be a regular file and have read permission */
        if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
            // Send an HTTP 403 Forbidden error response to the client (e.g., a web browser)
            clienterror(fd,filename,"403","Forbidden","Tiny couldn't find this file");
            return;   
        }
        serve_static(fd,filename,sbuf.st_size);
    }
    // Handle dynamic content (CGI)
    else {

        /* Must be a regular file and have execute permission */
        if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
            // Send an HTTP 403 Forbidden error response to the client (e.g., a web browser)
            clienterror(fd,filename,"403","Forbidden","Tiny couldn't run the CGI program");
            return;   
        }
        serve_dynamic(fd,filename,cgiargs);

    }

}

/*<------------------------------------------------------------------------------------------------------------------->*/

// void serve_static(int fd, char *filename, int filesize) {
//     int srcfd;
//     /* Heap buf to save file content */
//     char *filebuf;                       
//     char *srcp, filetype[MAXLINE], buf[MAXBUF];


//     get_filetype(filename, filetype);
//     sprintf(buf, "HTTP/1.0 200 OK\r\n");
//     sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
//     sprintf(buf, "%sConnection: close\r\n", buf);
//     sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
//     sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
//     Rio_writen(fd, buf, strlen(buf));

    
//     srcfd = Open(filename, O_RDONLY, 0);
//     filebuf = (char *)Malloc(filesize);
    
//     Rio_readn(srcfd, filebuf, filesize);
//     Rio_writen(fd, filebuf, filesize);
//     Free(filebuf);
//     Close(srcfd);
// }


//basic serve_static
void serve_static(int fd, char *filename, int filesize){
    /* File descriptor for finding the requested file on disk */
    int srcfd;
    /* Srcp : pointer to memory-mapped file */
    char *srcp, filetype[MAXLINE], buf[MAXBUF];


    // Send response headers to client 
    /* Determine the correct MIME type based on the file extension */
    get_filetype(filename, filetype);
    /* Compose the HTTP response header fields into buf */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    
    /* Send headers to client through the connected socket fd */
    Rio_writen(fd, buf, strlen(buf));
    
    /* Debug: print headers to server console */
    printf("Response headers:\n");
    printf("%s", buf);

    /* Send response body to client */
    /* Open the requested file in read-only mode */
    srcfd = Open(filename, O_RDONLY, 0);

    /* Map the requested file into memory for efficient I/O */
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);

    /* The file descriptor is no longer needed after mmap */
    Close(srcfd);

    /* Write the file content (response body) to the client */
    Rio_writen(fd, srcp, filesize);

    /* Unmap the file from memory,because srcp is no longer needed after sending via network */
    Munmap(srcp, filesize);
}


void serve_dynamic(int fd, char *filename, char *cgiargs){
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* First, send the HTTP status line and basic server information to the client via the connected socket (fd) */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));

    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Fork a child process; only the child will execute the CGI program */
    if (Fork() == 0) { /* Child */
        /* Pass the GET parameters to the CGI program via QUERY_STRING */
        setenv("QUERY_STRING", cgiargs, 1);
        /* Change print setting terminal into fd socket */
        Dup2(fd, STDOUT_FILENO);
        /* Replace the child process image with the CGI program specified by filename */
        Execve(filename, emptylist, environ);
    }
    /* Parent process waits until the child terminates */
    Wait(NULL);
}

/* Determine the Content-Type field for the HTTP response header */
void get_filetype(char *filename,char *filetype){
    /* Check the file extension and assign the corresponding MIME type */
    if (strstr(filename, ".html")) strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png")) strcpy(filetype, "image/png");
    else if (strstr(filename, ".mp4")) strcpy(filetype, "video/mp4");
    else if (strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
    else strcpy(filetype, "text/plain");
}



void clienterror(int fd, char *cause,char *errnum, char *shortmsg, char *longmsg){

    char buf[MAXLINE], body[MAXBUF];
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    /*Rio_written : send N bytes in buf to fd socket*/
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp){
    char buf[MAXLINE];
    /*Rio_readlineb : read strings until "\n" in rp and save buf*/
    Rio_readlineb(rp,buf,MAXLINE);
    /* read all HTTP header lines until a blank line ("\r\n") is encountered */
    while(strcmp(buf,"\r\n")){
        /*keep reading*/
        Rio_readlineb(rp,buf,MAXLINE);
        printf("%s",buf);
    }
    return;
}

int parse_uri(char *uri, char *filename, char *cgiargs){
    char *ptr;
    /* static content request: URI does not contain "cgi-bin" */
    if(!strstr(uri,"cgi-bin")){
         /* static requests do not include CGI arguments */
        strcpy(cgiargs,"");
        /*start from current directory*/
        strcpy(filename,".");
        /* Append the requested URI to the file path */
        strcat(filename, uri);
        /* If the URI ends with '/', append "home.html" as the default file */
        if(uri[strlen(uri)-1]=='/') strcat(filename, "home.html");
        return 1;
    }
    /* dynamic content request: URI contains "cgi-bin" */
    else{
        /*find question mark direction for */
        ptr = index(uri,'?');
        if (ptr){
            /*save */
            strcpy(cgiargs,ptr + 1);
            /*change question mark into "\0" to split filename part*/
            *ptr = '\0';
        }
        else{
            strcpy(cgiargs,"");
        }
        /*start from current directory*/
        strcpy(filename, ".");
        /* Append the requested URI to the file path */
        strcat(filename, uri);
        return 0;
    }
}



