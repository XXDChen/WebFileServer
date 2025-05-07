#ifndef CONN_POOL_H
#define CONN_POOL_H

#include <cstdio>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../locker.h"
using namespace std;

class connection_pool{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接
	//单例模式
	static connection_pool *GetInstance();
	void init(const char* url, const char* User, const char* PassWord, const char* DataBaseName, int Port, int MaxConn); 
	connection_pool();
	~connection_pool();

private:
	unsigned int MaxConn;  //最大连接数
	locker lock;
	list<MYSQL *> connList; //连接池
	sem reserve;
};

class connectionRAII{
public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif