#include <mysql/mysql.h>
#include <cstdio>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connpool.h"
using namespace std;

connection_pool::connection_pool(){
	MaxConn = 0;
}
connection_pool::~connection_pool(){
	DestroyPool();
}
connection_pool *connection_pool::GetInstance(){
	static connection_pool connPool;
	return &connPool;
}

void connection_pool::init(const char* url, const char* User, const char* PassWord, const char* DBName, int Port, int MaxConn){
	lock.lock();
	for (int i = 0; i < MaxConn; i++){
		MYSQL *con = NULL;
		con = mysql_init(con);
		if (con == NULL){
			exit(1);
		}
		con = mysql_real_connect(con, url, User, PassWord, DBName, Port, NULL, 0);
		if (con == NULL){
			exit(1);
		}
		connList.push_back(con);
	}
	reserve = sem(MaxConn);
	this->MaxConn = MaxConn;
	lock.unlock();
}

MYSQL *connection_pool::GetConnection(){
	MYSQL *con = NULL;
	if (connList.size() == 0)
		return NULL;
	reserve.wait();
	lock.lock();
	con = connList.front();
	connList.pop_front();
	lock.unlock();
	return con;
}

bool connection_pool::ReleaseConnection(MYSQL *con){
	if (NULL == con)
		return false;

	lock.lock();
	connList.push_back(con);
	lock.unlock();
	reserve.post();
	return true;
}

void connection_pool::DestroyPool(){
	lock.lock();
	if (connList.size() > 0){
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); it++){
			MYSQL *con = *it;
			mysql_close(con);
		}
		connList.clear();
		lock.unlock();
	}
	lock.unlock();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}