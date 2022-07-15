#include "server.h"

int Client::m_epollfd = -1;
int Client::m_user_count = 0;

//主线程维护users数组保存http连接对象以及它的一个定时器
Server::Server():m_port(9005),m_thread_num(16)
{
    users = new Client[MAX_FD];
    //线程池
    m_pool = new threadpool<Client>(m_thread_num);

    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;

    //转换为网络字节序大端对齐
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    //第三次挥手的时候会有个等待释放时间,端口不会迅速的被释放.
    //端口复用解决
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    //绑定本地IP和PORT
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);

    //监听
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    //epoll创建内核事件表
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    //将lfd注册进入内核事件表,不开启oneshot,lfd没有开启ET模式
    addfd(m_epollfd, m_listenfd, false,false);
    Client::m_epollfd = m_epollfd;

    //创建本地套接字
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    
    setnonblocking(m_pipefd[1]);
    //epoll_wait监视读端信号是否到达
    addfd(m_epollfd, m_pipefd[0], false, true);

    addsig(SIGPIPE, SIG_IGN);

    //若接受到SIGALRM和SIGTERM信号
    //定时时钟信号
    addsig(SIGALRM, sig_handler, false);
    //kill信号
    addsig(SIGTERM, sig_handler, false);

    alarm(everySec);
}

//析构函数
Server::~Server()
{
    close(m_epollfd);
    close(m_listenfd);
    delete[] users;
    delete m_pool;
    dumpfile();
}
//设置信号函数
void Server::addsig(int sig, void(*handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;

    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    //执行sigaction函数
    assert(sigaction(sig, &sa, NULL) != -1);
}
//信号处理函数
void sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    //将信号值从管道写端写入，传输字符类型
    send(m_pipefd[1], (char *)&msg, 1, 0);
    //将原来的errno赋值为当前的errno
    errno = save_errno;
}

void Server::addfd(int epollfd, int fd, bool one_shot,bool ET)
{
    epoll_event event;
    event.data.fd = fd;
    if(ET)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Client::addfd(int epollfd, int fd, bool one_shot,bool ET)
{
    epoll_event event;
    event.data.fd = fd;
    if(ET)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

int Server::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

int Client::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
bool Client::readTobuf()
{
    if (m_read_idx >= BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //ET模式一次性读完
    while (true)
    {
        //从套接字接收数据，存储在m_read_buf缓冲区
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            //非阻塞ET工作模式下，需要一次性将数据读完
            //此时读完会直接break循环
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        //修改m_read_idx的读取字节数
        m_read_idx += bytes_read;
    }
    return true;
}

bool Server::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
    if (connfd < 0)
    {
        return false;
    }
    //超过最大连接数量
    if (Client::m_user_count >= MAX_FD)
    {
        return false;
    }
    dealwithcreate(connfd, client_address);
    return true;
}
void Server::dealwithread(int sockfd)
{
    //proactor,主线程来读写,工作线程完成业务处理
    if (users[sockfd].readTobuf())
    {
        m_pool->append(users + sockfd);
    }
    else
    {
        dealwithclose(sockfd);
    }
}
//执行过程
void Client::process()
{
    struct in_addr ip;
    ip.s_addr = m_address.sin_addr.s_addr;
    char* client_ip = inet_ntoa(ip);
    //末尾添加'\0',不然转化string会乱码
    m_read_buf[m_read_idx] = '\0';
    
    LOG_OPERATION("Client(%s): '%s'",client_ip,m_read_buf);
    string message(m_read_buf);
    Command temp(message);
    temp.split_command();

    if(temp._arg[0] == "set")
    {
        ++delorset_count;
        string key = temp._arg[1];
        string value = temp._arg[2];
        bool flag = true;
        CommandFun cmd = commands["set"];
        //执行对应函数
        cmd(key,value,flag);
        //写入返回信息
        string str = "OK\n";
        strcpy(m_write_buf,str.c_str());
    }
    else if(temp._arg[0] == "get")
    {
        string key = temp._arg[1];
        string value = "";
        bool flag = true;
        CommandFun cmd = commands["get"];
        //执行对应函数
        cmd(key,value,flag);
        
        //写入返回信息
        string str = "";
        if(flag)
            str = "\""+ value + "\"\n";
        else
            str = "this key don't exist.\n";
        strcpy(m_write_buf,str.c_str());

    }
    else if(temp._arg[0] == "del")
    {
        ++delorset_count;
        string key = temp._arg[1];
        string value = "";
        bool flag = true;
        CommandFun cmd = commands["del"];
        //执行对应函数
        cmd(key,value,flag);
        
        //写入返回信息
        string str = "";
        if(flag)
            str = "del succeed.\n";
        else
            str = "this key don't exist.\n";
        strcpy(m_write_buf,str.c_str());

    }
    else if(temp._arg[0] == "load")
    {
        string key = "";
        string value = "";
        bool flag = true;
        CommandFun cmd = commands["load"];
        //执行对应函数
        cmd(key,value,flag);
        
        //写入返回信息
        string str = "load done.\n";
        strcpy(m_write_buf,str.c_str());

    }
    else if(temp._arg[0] == "dump")
    {
        string key = "";
        string value = "";
        bool flag = true;
        CommandFun cmd = commands["dump"];
        //执行对应函数
        cmd(key,value,flag);
        
        //写入返回信息
        string str = "dump done.\n";
        strcpy(m_write_buf,str.c_str());
    }
    else if(temp._arg[0] == "show")
    {
        string temp = "";
        string value = "";
        bool flag = true;
        CommandFun cmd = commands["show"];
        //执行对应函数
        cmd(temp,value,flag);

        //写入返回信息
        string str = "";
        if(flag)
            str = temp;
        else
            str = "no data exists.\n";
        strcpy(m_write_buf,str.c_str());
    }
    else if(temp._arg[0] == "clear")
    {
        delorset_count += 3;
        string temp = "";
        string value = "";
        bool flag = true;
        CommandFun cmd = commands["clear"];
        //执行对应函数
        cmd(temp,value,flag);

        //写入返回信息
        string str = "clear done.\n";
        strcpy(m_write_buf,str.c_str());
    }
    else
    {
        //写入返回信息
        string str = "Unknown command, please operate according to the client prompt.\n";
        strcpy(m_write_buf,str.c_str());
    }
    //注册写事件
    epoll_event event;
    event.data.fd = m_sockfd;
    event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(m_epollfd, EPOLL_CTL_MOD, m_sockfd, &event);
}
void Server::dealwithwrite(int sockfd)
{
    //proactor
    if(!users[sockfd].writeTouser())
    {
        dealwithclose(sockfd);
    }
}
void Server::dealwithclose(int sockfd)
{
    //删除内核空间描述符
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, sockfd, 0);
    //关闭该http文件描述符
    close(sockfd);
    cout<<sockfd<<" closed."<<endl;
    Client::m_user_count--;
}

void Server::dealwithsignal(bool &stop_server)
{
    char signals[1024] = {0};
    int buf_size = 1024;
    //读取的字节数
    int bytesRead = 0;
    //循环读取
    while(true)
    {
        if(bytesRead >= buf_size)
            break;
        int ret = recv(m_pipefd[0], signals + bytesRead, buf_size - bytesRead, 0);
        if(ret <= 0)
            break;
        bytesRead += ret;
    }

    //处理信号
    for(int i = 0; i < bytesRead; i++)
    {
        switch (signals[i])
            {
                case SIGALRM:
                {
                    ++time_count;
                    //每隔1秒检测一次,15秒到达或者增删次数超过3次,就进行数据落盘
                    if(time_count >= 15 || delorset_count >= 3)
                    {
                        do_scheduledTask();
                        time_count = 0;
                        delorset_count = 0;
                    }
                    alarm(everySec);
                    break;
                }
                case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
            }
    }   
}
void Server::do_scheduledTask()
{
    db.timed_dump();
}
void Client::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    cout<<sockfd<<" connected."<<endl;
    addfd(m_epollfd, sockfd, true, true);
    m_user_count++;
    init();
}
void Client::init()
{
    m_read_idx = 0;
    memset(m_read_buf,0,sizeof(m_read_buf));
    memset(m_write_buf,0,sizeof(m_write_buf));
}

void Server::dealwithcreate(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address);
}


void Server::environment_init()
{
    initCommand();
    Log::get_instance()->init("./log_file/log",4096,60000,900);
}

void setCommand(string &key, string& value, bool& flag)
{
    flag = db.insert_element(key, value);
}
void getCommand(string &key, string& value, bool& flag)
{
    flag = db.search_element(key, value);
}
void delCommand(string &key, string& value, bool& flag)
{
    flag = db.delete_element(key);
}
void loadCommand(string &key, string& value, bool& flag)
{
    db.load_file();
}
void dumpCommand(string &key, string& value, bool& flag)
{
    db.timed_dump();
}
void showCommand(string &str, string& value, bool& flag)
{
    flag = db.display_List(str);
}
void clearCommand(string&, string&, bool&)
{
    db.clear();
}

void initCommand()
{
    commands["set"] = &setCommand;
	commands["get"] = &getCommand;
    commands["del"] = &delCommand;
    commands["load"] = &loadCommand;
    commands["dump"] = &dumpCommand;
    commands["show"] = &showCommand;
    commands["clear"] = &clearCommand;
    db.load_file();
}

void Server::dumpfile()
{
    db.dump_file();
}

void Server::mainstart()
{
    bool stop_server = false;
    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        //忽略EINTR导致的阻塞返回,EINTR表示在读/写的时候出现了中断错误
        if (number < 0 && errno != EINTR)
        {
            break;
        }
        //遍历需要处理的事件
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                dealwithsignal(stop_server);
            }
            //处理异常事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //关闭连接
                dealwithclose(sockfd);
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            //将数据回写给用户
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
    }
}

bool Client::writeTouser()
{
    //已发送字节
    int bytes_write = 0;
    int size = strlen(m_write_buf);
    //ET模式写
    while(true)
    {
        int temp = 0;
        temp = send(m_sockfd,m_write_buf,strlen(m_write_buf),0);
        //单次发送不成功
        if(bytes_write < 0)
        {
            //可能是缓存区已满,重新注册事件再次发送
            if(errno == EAGAIN)
            {
                //重新注册写事件
                epoll_event event;
                event.data.fd = m_sockfd;
                event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
                epoll_ctl(m_epollfd, EPOLL_CTL_MOD, m_sockfd, &event);
                return true;
            }
            else
            {
                break;
            }
        }

        //发送成功,更新已发送字节
        bytes_write += temp;

        //若发送完毕
        if(bytes_write >= size)
        {
            //重新注册读事件
            epoll_event event;
            event.data.fd = m_sockfd;
            event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
            epoll_ctl(m_epollfd, EPOLL_CTL_MOD, m_sockfd, &event);
            init();
            return true;
        }
    }
    return false;
}
