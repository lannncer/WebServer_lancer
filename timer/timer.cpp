#include "timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
	head = NULL;
	tail = NULL;
}

sort_timer_lst::~sort_timer_lst()
{
	util_timer* p = head;
	while(p)
	{
		head = p->next;
		delete p;
		p = head;
	}
}

void sort_timer_lst::add_timer(util_timer *timer)//新连接，放到计时器队列里
{
	if(!timer)
		return;
	if(!head)
	{
		head = tail = timer;
		return;
	}
	if(timer->expire < head->expire)
	{
		timer->next = head;
		head->pre = timer;
		head = timer;
		return;
	}
	add_timer(timer,head);//插入到头结点之后
}

void sort_timer_lst::add_timer(util_timer *timer,util_timer *lst_head)
{
	util_timer* prev = lst_head;
	util_timer* tmp = prev->next;
	while(tmp)
	{
		if(timer->expire<tmp->expire)
		{
			prev->next = timer;
			timer->next = tmp;
			tmp->pre = timer;
			timer->pre = prev;
			break;
		}
		prev = tmp;
		tmp = tmp->next;
	}
	if(!tmp)
	{
		prev->next = timer;
		timer->pre = prev;
		timer->next = NULL;
		tail = timer;
	}
}

void sort_timer_lst::del_timer(util_timer *timer)//删除过期计时器
{
    if (!timer)
    {
        return;
    }
	if((timer == head)&&(timer == tail))
	{
		delete timer;
		head = NULL;
		tail = NULL;
		return;
	}
	if(timer == head)
	{
		head = head->next;
		head->pre = NULL;
		delete timer;
		return;
	}
	if(timer == tail)
	{
		tail = tail->pre;
		tail->next = NULL;
		delete timer;
		return;
	}
	timer->pre->next = timer->next;
	timer->next->pre = timer->pre;
	delete timer;
	return;
}

void sort_timer_lst::adjust_timer(util_timer *timer)//当连接有活动时，更新计时器位置
{
	if(!timer)
		return;
	util_timer* tmp = timer->next;
	if(!tmp||timer->expire<tmp->expire)
		return;
	if (timer == head)
    {
        head = head->next;
        head->pre = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
        add_timer(timer, timer->next);
    }
}


void sort_timer_lst::tick()//定时任务，移除过期的计时器
{
	if(!head)
		return;
	time_t cur = time(NULL);//获取当前时间
	util_timer* tmp = head;
	while(tmp)
	{
		if(cur<tmp->expire)
			return;
		tmp->cb_func(tmp->user_data);//回调函数断开http连接
		head = tmp->next;
        if (head)
        {
            head->pre = NULL;
        }		
        delete tmp;
        tmp = head;
	}
}

void Utils::init(sort_timer_lst timer_lst, int timeslot)
{
    m_timer_lst = timer_lst;
    m_TIMESLOT = timeslot;
}

//将文件描述符设置为非阻塞,ET模式文件描述符须为非阻塞，以防阻塞在一个事件上
int Utils::setnonblocking(int fd)
{
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}


//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}


void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
	 epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);//从eventfd中移除过期事件
	 assert(user_data);
	 close(user_data->sockfd);//关闭套接字
	 http_conn::m_user_count--;//连接数-1
}