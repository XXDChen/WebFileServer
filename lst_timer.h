#ifndef LST_TIMER_H
#define LST_TIMER_H
#include<cstdio>
#include<time.h>
#include<arpa/inet.h>
class util_timer;
struct client_data{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
    http_conn *user;
};
//定时器类
class util_timer{   
public:
    util_timer() : prev(NULL), next(NULL) {}
    void (*cb_func)(http_conn *); //回调函数，处理定时任务
    time_t expire;      //超时时间
    client_data *user_data;
    util_timer *prev;
    util_timer *next;
};
//定时器双向升序链表
class sort_timer_lst{
public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    ~sort_timer_lst(){
        util_timer *tmp = head;
        while (tmp){
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    void add_timer(util_timer *timer){  //定时器添加到链表中
        if(!timer){
            return;
        }
        if(!head){
            head = tail = timer;
            return;
        }
        if(timer->expire < head->expire){  //超时时间小于链表中所有定时器，作为头插入
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head); //重载函数，遍历插入
    }
    void adjust_timer(util_timer *timer){   //调整定时器节点位置(超时时间增加)
        if (!timer){
            return;
        }
        util_timer *tmp = timer->next;
        if (!tmp || (timer->expire < tmp->expire)){
            return;
        }
        if (timer == head){
            head = head->next;  //取出该节点
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head); //向后插入
        }
        else{
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }
    void del_timer(util_timer *timer){  //删除定时器节点
        if (!timer){
            return;
        }
        if ((timer == head) && (timer == tail)){
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        if (timer == head){
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if (timer == tail){
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    void tick(){    //处理链表上的到期任务
        if (!head){
            return;
        }
        printf( "timer tick\n" );
        //LOG_INFO("%s", "timer tick");
        //Log::get_instance()->flush();
        time_t cur = time(NULL);    //获取当前系统时间
        util_timer *tmp = head;
        while (tmp){
            if (cur < tmp->expire){
                break;
            }
            tmp->cb_func(tmp->user_data->user);   //执行回调函数
            head = tmp->next;
            if (head){
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(util_timer *timer, util_timer *lst_head){    //遍历插入lst_head后
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        while(tmp){
            if (timer->expire < tmp->expire){
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if(!tmp){   //遍历完作为尾部插入
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }
    util_timer *head;
    util_timer *tail;
};
#endif