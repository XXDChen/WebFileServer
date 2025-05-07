#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"
using namespace std;

class Log{
public:
    static Log *get_instance();
    static void *flush_log_thread(void *args);
    bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    void write_log(const char *format, ...);

private:
    Log();
    ~Log();
    void async_write_log();

private:
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数
    int m_today;
    FILE *m_fp;
    char *m_buf;
    block_queue<string> *m_log_queue;   //阻塞队列
    bool m_is_async;
    locker m_mutex;
};

#endif