#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;
//单例模式
class Log
{
public:
	//c++11以后，懒汉模式不需要加锁。
	//懒汉模式：需要调用时构造静态对象返回。
	//饿汉模式：一开始就构造静态对象

    static Log *get_instance()//返回静态对象
    {
        static Log instance;
        return &instance;
    }
	
	
    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);
	
    static void *flush_log_thread(void *args)//回调函数，异步写日志
    {
        Log::get_instance()->async_write_log();
    }

private:
    Log();//私有化构造函数
    virtual ~Log();
	
    void *async_write_log()
    {
		string single_log;
		//从阻塞队列中取出一个日志string，写入文件
		while(m_log_queue->pop(single_log))
		{
			m_mutex.lock();
			fputs(single_log.c_str(), m_fp);
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
    block_queue<string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    locker m_mutex;
    int m_close_log; //关闭日志
};


#define LOG_DEBUG(format, ...) if(0 == m_close_log) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) if(0 == m_close_log) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) if(0 == m_close_log) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) if(0 == m_close_log) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)
#endif