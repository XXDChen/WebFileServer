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
#include"lst_timer.h"
#include"./sql/sql_connpool.h"

#define MAX_FD 65535
#define MAX_EVENTUMBER 10000
#define TIMESLOT 5

extern void addfd(int epfd, int fd, bool oneshot);
extern void removefd(int epfd, int fd);
extern void setnonblock(int fd);
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epfd = 0;

void sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}
void timer_handler(){
    timer_lst.tick();
    alarm(TIMESLOT);
}
void cb_func(http_conn* users){   //关闭非活跃连接
    users->close_conn();
}
int main (int argc, char* argv[]){
    if(argc <= 1){
        printf("usage: %s port_number\n",basename(argv[0]));
        exit(-1);
    }
    int port = atoi(argv[1]);
    //捕捉SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);
    //创建数据库连接池
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "cxd", "998329", "webdb", 3306, 8);
    //创建线程池
    threadpool<http_conn> * pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    } catch(...){       //捕获所有异常
        return 1;
    }
    //数组保存客户端信息
    http_conn* users = new http_conn[MAX_FD];
    users->initmysql_result(connPool);
    
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
    epfd = epoll_create(1);
    addfd(epfd, lfd, false);
    http_conn::m_epollfd = epfd;
    //创建管道套接字
    socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    setnonblock(pipefd[1]);
    addfd(epfd, pipefd[0], false);
    addsig(SIGALRM, sig_handler);
    addsig(SIGTERM, sig_handler);
    client_data *users_timer = new client_data[MAX_FD];
    bool stop_server = false;
    bool timeout = false;
    alarm(TIMESLOT);    //定时5秒产生一个SIGALARM信号

    while(!stop_server){
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
                users_timer[cfd].address = caddr;
                users_timer[cfd].sockfd = cfd;
                users_timer[cfd].user = users + cfd;
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[cfd];   //设置回调函数 超时时间
                timer->cb_func = cb_func;       
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[cfd].timer = timer;
                timer_lst.add_timer(timer);
            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                users[sockfd].close_conn();
            } else if((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)){
                //处理管道信号
                int sig;
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret <= 0){
                    continue;
                }
                for(int j = 0; j < ret; j++){
                    switch (signals[i]){
                        case SIGALRM:
                            timeout = true;
                            break;
                        case SIGTERM:
                            stop_server = true;
                    }
                }
            } else if(events[i].events & EPOLLIN){
                util_timer *timer = users_timer[sockfd].timer;
                printf("---触发EPOLL---\n");
                if(users[sockfd].read()){   //一次性读完
                    pool->append(users + sockfd);
                    if (timer){
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                } else{
                    printf("读出错，关闭连接\n");
                    timer->cb_func(users + sockfd);
                    if (timer){
                        timer_lst.del_timer(timer);
                    }
                }
            } else if(events[i].events & EPOLLOUT){
                util_timer *timer = users_timer[sockfd].timer;
                if(users[sockfd].write()){    //一次性写完
                    if (timer){
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                }else{
                    if (timer){
                        timer->cb_func(users + sockfd);
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        if(timeout){    //最后处理定时事件
            timer_handler();
            timeout = false;
        }
    }
    close(epfd);
    close(lfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users;
    delete[] users_timer;
    delete pool;
    return 0;
}