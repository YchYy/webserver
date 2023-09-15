#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/epoll.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>
#include <cstring>

class http_conn
{
public:
    static int m_epollfd;   // 所有的socket上的事件都能被注册到同一个epoll对象中
    static int m_user_count;    //统计用户的数量
    static const int READ_BUFFER_SIZE = 2048;   //读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;  //写缓冲区的大小

    http_conn() {}
    ~http_conn() {}

    // HTTP 请求方法，目前只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    // 当前解析
    // CHECK_STATE_REQUESTLINE 请求行
    // CHECK_STATE_HEADER 请求头
    // CHECK_STATE_CONTENT 请求体
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};

    /*
        报文解析结果
        请求不完整，
        获得完整请求
        请求语法错误
        服务器没有资源
        没有足够的访问权限
        文件请求，获取文件成功
        服务器内部错误
        客户端关闭连接了
    */
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

    // 从状态机可能的状态
    // 读取到一个完整的行，行出错，行数据不完整
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

    void process(); //处理客户端的请求
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接收的连接
    void close_conn();  //关闭连接
    bool read();    // 非阻塞的读
    bool write();   // 非阻塞的写

    HTTP_CODE process_read();   // 解析http请求
    HTTP_CODE parse_request_line(char * text); // 解析请求首行
    HTTP_CODE parse_headers(char * text);   // 解析请求头
    HTTP_CODE parse_content(char * text);   // 解析请求体

    LINE_STATUS parse_line();

    char * get_line() {return m_read_buf + m_start_line;}

private:
    int m_sockfd;   //该http链接的socket
    sockaddr_in m_address;  // 通信的socket地址
    char m_read_buf[READ_BUFFER_SIZE];  // 读缓冲区
    int m_read_idx; //读取数据的下标
    char m_write_buf[WRITE_BUFFER_SIZE];

    int m_checked_idx;    // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;   // 当前正在解析的行的起始位置

    char * m_url;       // 请求目标文件的文件名
    char * m_version;   // 协议版本，目前只支持1.1
    METHOD m_method;    // 请求方法
    char * m_host;      // 主机名
    bool m_linger;      // http请求是否要保持连接


    CHECK_STATE m_check_state;  // 主状态机当前所处的状态

    void init();    // 初始化连接其余的

    HTTP_CODE do_request();
};

#endif