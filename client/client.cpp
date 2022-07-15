#include <arpa/inet.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cassert>
#include <unistd.h>

#include "command.h"
using namespace std;

int main(int argc,char* argv[])
{
    if(argc != 3)
    {
        cout<<"Please enter the IP address and port number."<<endl;
        exit(-1);
    }
    string server_ip = argv[1];
    string server_port = argv[2];

    //创建socket
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd != -1);

    struct sockaddr_in saddr;
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;

    //转换为网络字节序大端对齐
    saddr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    saddr.sin_port = htons(stoi(server_port));
    int res = connect(sockfd,(struct sockaddr*)&saddr,sizeof(saddr));
    assert(res != -1);

    cout<<"Select an input from the following five commands:"<<endl;
    cout<<"set key value, del key, get key, load, dump, show, clear, quit"<<endl;
    while(true)
    {
        string message = "";
        cout<<server_ip<<":"<<server_port<<"> ";
        cin.sync();
        getline(cin,message);
        //检测命令是否合法
        Command comd(message);
        if(!comd.is_valid_command())
            continue;
        
        //发送命令
        char send_buf[1024];
        memset(send_buf,0,sizeof(send_buf));

        strcpy(send_buf,message.c_str());
        send(sockfd,send_buf,strlen(send_buf),0);

        char rev_buf[1024];
        memset(rev_buf,0,sizeof(rev_buf));

        int ret = recv(sockfd,rev_buf,sizeof(rev_buf),0);
        if(ret <= 0)
        {
            cout<<"Server closed!"<<rev_buf<<endl;
            break;
        }
        cout<<rev_buf;
    } 
    close(sockfd);

    return 0;
}