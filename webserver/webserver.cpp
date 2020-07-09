#include "webserver.h"

WebServer::WebServer()
{
	//http连接对象
	users = new http_conn[MAX_FD];
	
	//html根文件目录
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

//释放内存防止内存泄漏
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, int sqlverify,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_SQLVerify = sqlverify;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::log_write()//启动日志
{
	    if (m_close_log == 0)
    {
        //初始化日志
        if (m_log_write == 1)//日志系统为单例模式，返回一个静态对象
		//1文件名，2日志开关，3日志缓冲区大小，4日志最大行数，5阻塞队列最大长度
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()//启动数据库连接
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();//单例模式
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num);

    //初始化数据库读取表
    if (m_SQLVerify == 0)
        users->initmysql_result(m_connPool);
    else if (m_SQLVerify == 1)
        users->initresultFile(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池，业务对象为http_conn
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
	//socket编程
	m_listenfd = socket(PF_INET,SOCK_STREAM,0);//创建监听套接字
	assert(m_listenfd >= 0);

    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};//l_onoff为0,close立即返回
		//setsockopt对套接字进行设置
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};//l_onoff为1，l_linger为1
		/*当套接口关闭时内核将拖延一段时间（由l_linger决定）。
		如果套接口缓冲区中仍残留数据，进程将处于睡眠状态，直 到（a）所有数据发送完且被对方确认，
		之后进行正常的终止序列（描述字访问计数为0）或（b）延迟时间到。
		此种情况下，应用程序检查close的返回值是 非常重要的，
		如果在数据发送完并被确认前时间到，close将返回EWOULDBLOCK错误且套接口发送缓冲区中的任何数据都丢失。
		close的成功返 回仅告诉我们发送的数据（和FIN）已由对方TCP确认，它并不能告诉我们对方应用进程是否已读了数据。
		如果套接口设为非阻塞的，它将不等待close完成*/
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
	
	int ret = 0;
	struct sockaddr_in address;
	bzero(&address,sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);//127.0.0.1
	address.sin_port = htons(m_port);
	
	int flag = 1;
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	ret = bind(m_listenfd, (struct sockaddr*)&address,sizeof(address));
	assert(ret >= 0);
	ret = listen(m_listenfd, 5);
	assert(ret >= 0);
	
	//创建信号，管道，用于定时器触发信号
	Utils::u_pipefd = m_pipefd;
	Utils::u_epollfd = m_epollfd;
	utils.init(timer_lst,TIMESLOT);
	
    //epoll创建内核事件表
    //epoll_event events[MAX_EVENT_NUMBER];
	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);
	
	utils.addfd(m_epollfd,m_listenfd,false,m_TRIGMode);//注册listenfd
	http_conn::m_epollfd = m_epollfd;
	
	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);//信号管道
	assert(ret != -1);
	
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, m_TRIGMode);
	
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);
	
	alarm(TIMESLOT);
}


void WebServer::eventLoop()
{
	bool timeout = false;
	bool stop_server = false;
	
	while(!stop_server)
	{
		int number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
		if (number < 0 && errno != EINTR)
		{
            LOG_ERROR("%s", "epoll failure");//输出日志
            break;			
		}
		
		for(int i = 0;i<number;i++)
		{
			int sockfd = events[i].data.fd;
			
			//处理新连接
			if(sockfd == m_listenfd)
			{
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;				
			}
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))//EPOLLRDHUP客户端断开连接，EPOLLHUP读写都关闭，EPOLLERR错误
			{
				//服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
			}
			//处理信号
			else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
			{
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    continue;				
			}
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
			//事件可写
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
		}
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");
            Log::get_instance()->flush();

            timeout = false;
        }
	}
}

//处理新连接
bool WebServer::dealclinetdata()
{
	struct sockaddr_in client_address;
	socklen_t client_addrlength = sizeof(client_address);
	
    if (m_TRIGMode == 0)//LT模式
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)//超过最大连接数
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
	else//ET模式
	{
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
	users[connfd].init(connfd, client_address, m_root, m_SQLVerify, m_TRIGMode, m_close_log, m_user, m_passWord, m_databaseName);
	
	//初始化Client_data
	users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
	
	util_timer *timer = new util_timer;//创建定时器
	timer->user_data = &users_timer[connfd];
	timer->cb_func = cb_func;//设置回调函数
	
	time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;//设置超时时间
	
	users_timer[connfd].timer = timer;
    timer_lst.add_timer(timer);
}


bool WebServer::dealwithsignal(bool timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);	
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;//定时器触发信号
                break;
            }
            case SIGTERM:
            {
                stop_server = true;//关闭服务器
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
	util_timer *timer = users_timer[sockfd].timer;
	
	//reactor模式
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);//调整计时器队列
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            if (users[sockfd].improv == 1)
            {
                if (users[sockfd].timer_flag == 1)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor模式
        if (users[sockfd].read_once())//在主线程进行i/o操作
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            Log::get_instance()->flush();

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

//处理可读事件
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor模式
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (users[sockfd].improv == 1)
            {
                if (users[sockfd].timer_flag == 1)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor模式
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            Log::get_instance()->flush();

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}


void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
    Log::get_instance()->flush();
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
    Log::get_instance()->flush();
}



