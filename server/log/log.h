//异步日志系统
#ifndef LOG_H
#define LOG_H

#include <cstdio>
#include <iostream>
#include <string>
#include <cstring>
#include <time.h>
#include <sys/time.h>
#include <cstdarg>

#include "block_que.hpp"

using namespace std;

class Log
{
public:
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    //初始化文件
    bool init(const char *file_name,int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 1000);

    //将要输出的信息按标准格式化后存入阻塞队列等待写线程写入文件
    void write_log(const char *format, ...);

    //强制刷新缓冲区
    void flush(void);

private:
    Log();
    virtual ~Log();

    //异步写日志方法
    void *async_write_log()
    {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            //刷新输出缓冲区写入磁盘
            fflush(m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;
    locker m_mutex;
    block_queue<string> *m_log_queue; //阻塞队列
};

//在其他文件中使用，主要用于不同类型的日志输出
#define LOG_OPERATION(format, ...){Log::get_instance()->write_log(format, ##__VA_ARGS__);}
#endif
