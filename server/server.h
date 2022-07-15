#ifndef SERVER_H
#define SERVER_H

#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <cassert>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <vector>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>

#include "./skiplist/skiplist.hpp"
#include "./threadpool/threadpool.hpp"
#include "./command/command.h"
#include "./log/log.h"

using namespace std;
typedef skipList<string,string> myDB;
typedef void (*CommandFun) (string&,string&,bool&);

const int MAX_FD = 65536;           
const int MAX_EVENT_NUMBER = 15000; 
const int dbNum = 16;
static const int BUFFER_SIZE = 2048;
static unordered_map<string,CommandFun> commands;
static myDB db;
static int m_pipefd[2];
static const int everySec = 1;
static int delorset_count = 0;
static int time_count = 0;

//初始化命令
void setCommand(string&, string&, bool&);
void getCommand(string&, string&, bool&);
void delCommand(string&, string&, bool&);
void loadCommand(string&, string&, bool&);
void dumpCommand(string&, string&, bool&);
void showCommand(string&, string&, bool&);
void clearCommand(string&, string&, bool&);
void initCommand();
void sig_handler(int sig);

class Client{
public:
    Client(){}
    ~Client(){}

    void init(int sockfd, const sockaddr_in &addr);
    void addfd(int epollfd, int fd, bool one_shot, bool ET);
    int setnonblocking(int fd);
    bool readTobuf();
    bool writeTouser();
    void process();

    static int m_epollfd;
    static int m_user_count;
    char m_read_buf[BUFFER_SIZE];
    char m_write_buf[BUFFER_SIZE];
    sockaddr_in my_client;
    int m_read_idx;
private:
    int m_sockfd;
    sockaddr_in m_address;
    void init();
};

class Server
{
public:
    Server();
    ~Server();
    void addfd(int epollfd, int fd, bool one_shot, bool ET);
    int setnonblocking(int fd);
    void addsig(int sig, void(handler)(int), bool restart = true);
    bool dealclinetdata();
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);
    void dealwithclose(int sockfd);
    void dealwithcreate(int connfd, struct sockaddr_in client_address);
    void dealwithsignal(bool &stop_server);
    void do_scheduledTask();
    void environment_init();
    void mainstart();
    void dumpfile();
public:
    int m_port;
    Client* users;
    int m_epollfd;
    sockaddr_in local;

    threadpool<Client> *m_pool;
    int m_thread_num;

    epoll_event events[MAX_EVENT_NUMBER];
    int m_listenfd;
};

#endif