#include "http_conn.h"

 int http_conn::m_epollfd = -1;   // 所有的socket上的事件都能被注册到同一个epoll对象中
     int http_conn::m_user_count = 0;    //统计用户的数量

// 设置文件描述符为非阻塞
int setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    old_flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, old_flag);
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
//    event.events = EPOLLIN | EPOLLRDHUP;    //-----
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;    //-----


    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符为非阻塞
    setnonblocking(fd);
}

// 从epoll中移除监听的文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
// 修改文件描述符， 重置socket上的EOPLLONESHOT事件， 以确保下一次可读时， EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);

}

// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in & addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    // 设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // 添加到epoll对象中
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始化状态为解析请求首行
    m_checked_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;

    bzero(m_read_buf, READ_BUFFER_SIZE);
}

//关闭连接
void http_conn::close_conn()
{
    if (m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count --;
    }
}

bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0;
    while(true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 没有数据
                break;
            }
            return false;
        }
        else if (bytes_read == 0)
        {
            // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    printf("读取到数据: \n%s\n", m_read_buf);

    return true;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char *text = 0;

    while( ((line_status = parse_line()) == LINE_OK) || 
        (line_status == LINE_OK && m_check_state == CHECK_STATE_CONTENT) )
    {
        // 解析到了一行完整的数据，或者解析到了请求体
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line : %s\n", text);

        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST)
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: 
            {
                break;
                // return INTERNAL_ERROR;  // nt
            }
        }
        return NO_REQUEST;
    }

    return NO_REQUEST;
}

// 解析http请求行，获得请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char * text)
{
    m_url = strpbrk(text, " \t");
    *m_url++ = '\0';

    char * method = text;

    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else 
    {
        return BAD_REQUEST;
    }

    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';

    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // 忽略大小写 比较前7个字符
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')  return BAD_REQUEST;

    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头

    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_headers(char * text)
{
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char * text)
{
    return NO_REQUEST;
}

// 解析一行数据
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;

    for(; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];

        if (temp == '\r')
        {
            if (m_checked_idx + 1 == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            // 个人感觉这步操作很 nt
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        return LINE_OPEN;   // ?????
    }
    return LINE_OK;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    return NO_REQUEST;
}


// 由线程池的工作线程调用的，http的入口函数
void http_conn::process()
{
    // 解析http请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    
    printf("parse request, create response");
    // 生成相应
}

bool http_conn::write()
{
    printf("一次性写完所有数据");
    return true;
}