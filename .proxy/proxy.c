/*
원래는 클라이언트 요청 -> 서버로 가서 -> 서버가 응답 처리 -> 클라이언트로 표시
클라이언트 요청 -> 프록시 서버로 와서 요청 파싱 -> 웹서버에 연결  -> 프록시가 서버 응답 읽고 -> 클라이언트로 처리
*/

/* proxy.c — CS:APP Proxy Lab (Sequential Web Proxy)
 *
 * 요구사항 요약
 *  - HTTP/1.0 GET만 지원
 *  - 브라우저가 보낸 요청 라인을 파싱해서 호스트/포트/경로 분리
 *  - 서버쪽으로 보낼 요청은 "GET <path> HTTP/1.0\r\n"
 *  - 헤더 정책:
 *      * Host: 반드시 포함 (브라우저가 보낸 게 없으면 추가, 있으면 그대로 사용)
 *      * User-Agent: 과제에서 제공된 고정 문자열로 강제
 *      * Connection: close
 *      * Proxy-Connection: close
 *      * 그 외 헤더는 원본 그대로 전달
 *  - 요청/응답은 한 번의 왕복 후 연결 종료 (close)
 *  - 순차 처리 (멀티스레딩/캐시 없음)
 */

#include "csapp.h"
#include <ctype.h>

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
    "Gecko/20120305 Firefox/10.0.3\r\n";


static void doit(int connfd);
static int  parse_uri(const char *uri, char *host, char *port, char *path);
static void build_and_forward_request(int connfd,
                                      const char *method,
                                      const char *uri,
                                      const char *version,
                                      rio_t *client_rio);
static void send_client_error(int fd, const char *errnum,
                              const char *shortmsg, const char *longmsg);


int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    /* Open the fd listened by proxy to receive client request */
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen,
                    client_hostname, MAXLINE,
                    client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n",
               client_hostname, client_port);

        doit(connfd);       /* 순차 처리 */
        Close(connfd);
    }
}

/* 하나의 클라이언트 요청을 처리 */
static void doit(int connfd) {
    rio_t rio;
    char buf[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    Rio_readinitb(&rio, connfd);

    /* 요청 라인 읽기 */
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        return; 
    }

    
    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        send_client_error(connfd, "400", "Bad Request",
                          "Proxy could not parse request line");
        return;
    }

    
    if (strcasecmp(method, "GET")) {
        send_client_error(connfd, "501", "Not Implemented",
                          "Proxy does not implement this method");
        
        while (Rio_readlineb(&rio, buf, MAXLINE) > 0 && strcmp(buf, "\r\n"))
            ;
        return;
    }

    
    build_and_forward_request(connfd, method, uri, version, &rio);
}

/* URI를 host/port/path로 분리
 * 지원 형태:
 *   http://host[:port]/path...
 *   https 미지원(과제 사양)
 *   절대경로만 온다고 가정 (브라우저는 proxy에 절대URI를 보냄)
 * 반환: 0 성공, -1 실패
 */
static int parse_uri(const char *uri, char *host, char *port, char *path) {
    const char *p = uri;

    /* 스킴 검사 */
    const char *prefix = "http://";
    size_t plen = strlen(prefix);
    if (strncasecmp(p, prefix, plen) == 0) {
        p += plen;
    } else {
        /* 절대URI가 아닌 경우도 있을 수 있으므로 호스트 없음 처리 */
        /* 이 경우 Host 헤더로 호스트를 알아야 한다. 여기서는 실패 반환. */
        return -1;
    }

    /* host[:port][/<path>] */
    const char *host_beg = p;
    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : uri + strlen(uri);

    /* host:port 분할 */
    const char *colon = memchr(host_beg, ':', host_end - host_beg);
    if (colon) {
        size_t hlen = colon - host_beg;
        memcpy(host, host_beg, hlen);
        host[hlen] = '\0';
        size_t plen2 = host_end - (colon + 1);
        memcpy(port, colon + 1, plen2);
        port[plen2] = '\0';
    } else {
        size_t hlen = host_end - host_beg;
        memcpy(host, host_beg, hlen);
        host[hlen] = '\0';
        strcpy(port, "80");
    }

    /* path */
    if (slash) {
        strcpy(path, slash);     /* leading '/' 포함 */
    } else {
        strcpy(path, "/");
    }

    return 0;
}

/* 요청을 재구성해서 원서버로 전송하고, 응답을 클라이언트로 중계 */
static void build_and_forward_request(int connfd,
                                      const char *method,
                                      const char *uri,
                                      const char *version_in,
                                      rio_t *client_rio) {
    char host[MAXLINE], port[16], path[MAXLINE];
    char reqline[MAXLINE];
    char buf[MAXLINE], hdrs[MAXLINE];
    int has_host = 0;

    if (parse_uri(uri, host, port, path) < 0) {
        /* 절대URI가 아닌 경우: Host 헤더에서 보충 시도 */
        /* 간단 구현: 실패 처리 */
        send_client_error(connfd, "400", "Bad Request",
                          "Proxy expects absolute URI starting with http://");
        /* 남은 헤더 비우기 */
        while (Rio_readlineb(client_rio, buf, MAXLINE) > 0 && strcmp(buf, "\r\n"))
            ;
        return;
    }

    /* 서버 연결 */
    int serverfd = Open_clientfd(host, port);
    if (serverfd < 0) {
        send_client_error(connfd, "502", "Bad Gateway",
                          "Proxy could not connect to end server");
        /* 남은 헤더 비우기 */
        while (Rio_readlineb(client_rio, buf, MAXLINE) > 0 && strcmp(buf, "\r\n"))
            ;
        return;
    }

    rio_t server_rio;
    Rio_readinitb(&server_rio, serverfd);

    /* 요청라인: 서버에는 HTTP/1.0으로 다운그레이드 */
    /* (과제 지침: robust parser가 아니어도 되고, 1.1 들어와도 1.0으로 보냄) */
    snprintf(reqline, sizeof(reqline), "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, reqline, strlen(reqline));

    /* 클라이언트로부터 헤더를 읽어, 정책에 맞춰 서버로 전달 */
    /* 금지/대체되는 헤더: Connection, Proxy-Connection, User-Agent
       Host는 있으면 그대로 하나만 유지, 없으면 나중에 추가 */
    hdrs[0] = '\0';
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n")) break; /* 헤더 끝 */

        if (!strncasecmp(buf, "Host:", 5)) {
            has_host = 1;
            /* Host는 그대로 보냄(후중복 방지 위해 즉시 전송) */
            Rio_writen(serverfd, buf, strlen(buf));
            continue;
        }
        if (!strncasecmp(buf, "Connection:", 11)) continue;
        if (!strncasecmp(buf, "Proxy-Connection:", 17)) continue;
        if (!strncasecmp(buf, "User-Agent:", 11)) continue;

        /* 기타 헤더는 그대로 전달 */
        Rio_writen(serverfd, buf, strlen(buf));
    }

    /* 필수 헤더 강제 */
    if (!has_host) {
        char hosthdr[MAXLINE];
        snprintf(hosthdr, sizeof(hosthdr), "Host: %s\r\n", host);
        Rio_writen(serverfd, hosthdr, strlen(hosthdr));
    }
    Rio_writen(serverfd, user_agent_hdr, strlen(user_agent_hdr));
    Rio_writen(serverfd, "Connection: close\r\n", strlen("Connection: close\r\n"));
    Rio_writen(serverfd, "Proxy-Connection: close\r\n", strlen("Proxy-Connection: close\r\n"));

    /* 헤더 종료 빈 줄 */
    Rio_writen(serverfd, "\r\n", 2);

    /* 서버 응답을 읽어 클라이언트로 중계 */
    ssize_t n;
    char respbuf[MAXBUF];
    while ((n = Rio_readnb(&server_rio, respbuf, sizeof(respbuf))) > 0) {
        Rio_writen(connfd, respbuf, n);
    }

    Close(serverfd);
}

/* 간단한 오류 응답 */
static void send_client_error(int fd, const char *errnum,
                              const char *shortmsg, const char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* response body */
    snprintf(body, sizeof(body),
             "<html><title>Proxy Error</title>"
             "<body bgcolor=\"ffffff\">"
             "%s: %s\r\n"
             "<p>%s</p>"
             "<hr><em>CS:APP Web Proxy</em>\r\n"
             "</body></html>", errnum, shortmsg, longmsg);

    /* response headers + body */
    snprintf(buf, sizeof(buf), "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "Content-length: %zu\r\n\r\n", strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}