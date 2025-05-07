#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log(){
    m_count = 0;
    m_is_async = false;
}
Log::~Log(){
    if (m_fp != NULL){
        fclose(m_fp);
    }
}
Log *Log::get_instance(){
    static Log instance;
    return &instance;
}
void Log::async_write_log(){
        string single_log;
        while (m_log_queue->pop(single_log)){   //阻塞队列中取数据写入
            m_mutex.lock();
            int n = fputs(single_log.c_str(), m_fp);
            fflush(m_fp);
            m_mutex.unlock();
        }
}
void *Log::flush_log_thread(void *args){
    get_instance()->async_write_log();
}

bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size){
    if (max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;
    strcpy(log_name, file_name);
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    m_today = my_tm.tm_mday;

    char log_full_name[256] = {0};
    snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL){
        return false;
    }
    return true;
}

void Log::write_log(const char *format, ...){
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    m_mutex.lock();
    m_count++;
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) { //分文件
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char t[16] = {0};
        snprintf(t, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        if (m_today != my_tm.tm_mday){
            snprintf(new_log, 255, "%s%s", t, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }else{
            snprintf(new_log, 255, "%s%s.%lld", t, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);
    string log_str;
    m_mutex.lock();
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec);
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    }else{
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        fflush(m_fp);
        m_mutex.unlock();
    }
    va_end(valst);
}