#include "./webserver/webserver.h"
#include <iostream>

using namespace std;

class Config
{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char*argv[]);

    //端口号
    int PORT;

    //数据库校验方式
    int SQLVerify;

    //日志写入方式
    int LOGWrite;

    //触发模式
    int TRIGMode;

    //优雅关闭链接
    int OPT_LINGER;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    //是否关闭日志
    int close_log;

    //并发模型选择
    int actor_model;
};

Config::Config(){
    //端口号,默认9006
    PORT = 9006;

    //数据库校验方式，默认同步
    SQLVerify = 0;

    //日志写入方式，默认同步
    LOGWrite = 0;

    //触发模式，默认LT
    TRIGMode = 0;

    //优雅关闭链接，默认不使用
    OPT_LINGER = 0;

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量,默认8
    thread_num = 8;

    //关闭日志,默认不关闭
    close_log = 0;

    //并发模型,默认是proactor
    actor_model = 0;
}

void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:v:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        }
        case 'v':
        {
            SQLVerify = atoi(optarg);
            break;
        }
        case 'l':
        {
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}

int main(int argc, char *argv[])
{
	//数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "19951226";
    string databasename = "yourdb";
	
    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, config.SQLVerify,
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
	
    //cout<<1<<endl;
    //日志
    server.log_write();
	//cout<<2<<endl;
    //数据库
    server.sql_pool();
	//cout<<3<<endl;
    //线程池
    server.thread_pool();
	//cout<<4<<endl;
    //监听
    server.eventListen();
	//cout<<5<<endl;
    //运行
    server.eventLoop();
	//cout<<6<<endl;

    return 0;
}
