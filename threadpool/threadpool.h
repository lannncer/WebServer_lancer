#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../Lock/Locker.h"
#include "../CGImysql/sql_connection_pool.h"

//线程池类模板

template <typename T>//T为http业务对象
class threadpool
{
public:
	//action_model运行模式，connPool数据库连接，thread_num线程池中线程数量，max_req是请求队列中最多允许的、等待处理的请求的数量
	threadpool(int action_model,connection_pool *connPool,int thread_num = 8,int max_req = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);
	
private:
    //工作线程运行的函数，它不断从工作队列中取出任务并执行之
    static void *worker(void *arg);
    void run();
	
private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};

//构造函数
template <typename T>
threadpool<T>::threadpool(int action_model,connection_pool *connPool,int thread_num ,int max_req ) : m_actor_model(action_model),m_thread_number(thread_num),m_max_requests(max_req),m_threads(NULL),m_connPool(connPool)
{
	if(m_thread_number<=0||m_max_requests<=0)
		throw std::exception();
	//线程id初始化
	m_threads = new pthread_t[m_thread_number];
	if(!m_threads)
		throw std::exception();
	for(int i = 0;i<m_thread_number;i++)
	{
		//循环创建线程，并将工作线程按要求进行运行
		if(pthread_create(m_threads+i,NULL,worker,this)!=0)
		{
			delete[] m_threads;
			throw std::exception();
		}
		
		//将线程进行分离后，不用单独对工作线程进行回收
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
	}
}

//析构函数
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

//向任务队列中插入任务，并通过信号量通知工作线程处理任务
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
	m_queuelocker.lock();//保证线程安全
	if(m_workqueue.size()>=m_max_requests)//当前任务队列超过最大数量，返回false
	{
		m_queuelocker.unlock();
		return false;
	}
	request->m_state = state;
	m_workqueue.push_back(request);//向任务队列尾部插入
	m_queuelocker.unlock();
	m_queuestat.post();//通知工作线程处理
	return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//线程池工作线程，处理http请求
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
	while(true)
	{
		m_queuestat.wait();//等待信号量，去处理任务
		m_queuelocker.lock();//加锁保证线程安全
		if(m_workqueue.empty())
		{
			m_queuelocker.unlock();
			continue;
		}
		T *request = m_workqueue.front();//取出队列头的任务
		m_workqueue.pop_front();
		m_queuelocker.unlock();
		if(!request)
			continue;
		if(m_actor_model == 1)//并发模型为1时，为reactor模式
		{
            if (0 == request->m_state)//0为读
            {
                if (request->read_once())//reactor模式在工作线程处理i/o请求和逻辑处理
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else//1为写
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
		}
        else//并发模型为0时，为proactor模式
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
	}
}
#endif