#ifndef HTTPCONN_H
#define HTTPCONN_H
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/mman.h>
#include<sys/stat.h>
#include<sys/uio.h>
#include<stdarg.h>
#include<errno.h>
#include<stdarg.h>
#include<fcntl.h>
#include<string.h>
#include "locker.h"
class http_conn{
public:
    static int m_epollfd;   //所有socket事件注册到同一个epfd上
    static int m_user_count;
    static const int READ_BUFSIZE = 1024 * 5;
    static const int WRITE_BUFSIZE = 1024;
    static const int FILENAME_LEN = 200;
    //HTTP请求方法
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    //主状态机状态（分析请求行、请求报头、请求主体）
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    //从状态机状态（行完整、行出错、行不完整）
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
    //报文解析的结果
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    http_conn(){}
    ~http_conn(){}
    void process();     //处理客户端请求
    void init(int sockfd, sockaddr_in &addr);   //初始化连接
    void close_conn();    //关闭连接
    bool read();    //非阻塞读
    bool write();   //非阻塞写、

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_readbuf[READ_BUFSIZE];
    char m_readbuf1[READ_BUFSIZE];
    int m_readidx;    //读位置
    char m_writebuf[WRITE_BUFSIZE];
    int m_writeidx;    //写位置
    struct iovec m_iv[2];   //两块内存分散写
    int m_iv_count;
    int byte_have_send;    // 已经发送的字节
    int byte_to_send;    //将要发送的字节
    int m_checkedidx;   //当前分析字符在读buf的位置
    int m_startline;    //当前分析行的起始位置
    CHECK_STATE m_check_state;  //主状态机状态
    char m_realfile[FILENAME_LEN];  //目标文件完整路径
    char * m_url;       //请求目标文件名
    struct stat m_filestat;    //目标文件状态
    char * m_fileaddr;  //目标文件内存映射位置
    char * m_version;   //协议版本
    char * m_host;      //主机名
    bool m_linger;      //是否保持连接
    METHOD m_method;    //请求方法
    int m_content_length;  //请求体长度

    void init();    //初始化其他信息
    HTTP_CODE process_read();   //解析HTTP请求
    bool process_write(HTTP_CODE ret);  //填充HTTP响应

    HTTP_CODE parse_request_line(char* text);  // 解析HTTP请求首行
    HTTP_CODE parse_headers(char* text);  // 解析HTTP请求头
    HTTP_CODE parse_content(char* text);  // 解析HTTP请求主体
    HTTP_CODE do_request();     //处理请求
    LINE_STATUS parse_line();   //从状态机解析单行
    char * get_line(){return m_readbuf + m_startline;}  //获取行首字符

    void unmap();
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char* content);
    bool add_content_type();
};
#endif