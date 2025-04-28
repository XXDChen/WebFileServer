#include<cstdio>
#include<stdlib.h>
#include<string.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<signal.h>
#include"locker.h"
#include"threadpool.h"
#include"http_conn.h"

#define MAX_FD 65535
#define MAX_EVENTUMBER 10000

extern void addfd(int epfd, int fd, bool oneshot);
extern void removefd(int epfd, int fd);
extern void modfd(int epfd, int fd, int ev);

void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}
int main (int argc, char* argv[]){
    if(argc <= 1){
        printf("usage: %s port_number\n",basename(argv[0]));
        exit(-1);
    }
    int port = atoi(argv[1]);
    //捕捉SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);
    //创建线程池
    threadpool<http_conn> * pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    } catch(...){       //捕获所有异常
        return 1;
    }
    //数组保存客户端信息
    http_conn* users = new http_conn[MAX_FD];

    int lfd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(lfd, (struct sockaddr *)&saddr, sizeof(saddr));
    listen(lfd, 50);

    struct epoll_event events[MAX_EVENTUMBER];
    int epfd = epoll_create(1);
    addfd(epfd, lfd, false);
    http_conn::m_epollfd = epfd;

    while(1){
        int num = epoll_wait(epfd, events, MAX_EVENTUMBER, -1);
        if(num < 0 && errno != EINTR){
            printf("epoll failure\n");
            break;
        }
        for(int i = 0; i < num; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == lfd){
                struct sockaddr_in caddr;
                socklen_t slen = sizeof(caddr);
                int cfd = accept(lfd, (struct sockaddr *)&caddr, &slen);
                if(http_conn::m_user_count >= MAX_FD){
                    close(cfd);
                    continue;
                }
                users[cfd].init(cfd, caddr);
            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                users[sockfd].close_conn();
            } else if(events[i].events & EPOLLIN){
                printf("---触发EPOLL---\n");
                if(users[sockfd].read()){   //一次性读完
                    pool->append(users + sockfd);
                } else{
                    printf("读出错，关闭连接\n");
                    users[sockfd].close_conn();
                }
            } else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].write()){    //一次性写完
                    users[sockfd].close_conn();
                }
            }
        }
    }
    close(epfd);
    close(lfd);
    delete [] users;
    delete pool;
    return 0;
}