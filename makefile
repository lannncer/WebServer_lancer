server: main.cpp ./timer/timer.h ./timer/timer.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./Lock/Locker.h ./log/log.cpp ./log/log.h ./log/block_queue.h ./CGImysql/sql_connection_pool.cpp ./CGImysql/sql_connection_pool.h ./webserver/webserver.h ./webserver/webserver.cpp 
	g++ -o server main.cpp ./timer/timer.h ./timer/timer.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./Lock/Locker.h ./log/log.cpp ./log/log.h ./CGImysql/sql_connection_pool.cpp ./CGImysql/sql_connection_pool.h ./webserver/webserver.h ./webserver/webserver.cpp  -lpthread -L/usr/lib64/mysql/ -lmysqlclient -g

CGISQL.cgi:./CGImysql/sign.cpp ./CGImysql/sql_connection_pool.cpp ./CGImysql/sql_connection_pool.h
	g++ -o ./root/CGISQL.cgi ./CGImysql/sign.cpp ./CGImysql/sql_connection_pool.cpp ./CGImysql/sql_connection_pool.h -L/usr/lib64/mysql/ -lmysqlclient -lpthread

clean:
	rm  -r server
	rm  -r ./root/CGISQL.cgi
