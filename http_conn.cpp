#include"http_conn.h"
// HTTP响应状态信息
#define BUF_SIZE 65536
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

const char* doc_root = "/home/ubuntu/cxd/webserver/resources";  //资源根目录
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
//将表中的用户名和密码放入map
map<string, string> users;
locker m_lock;

void http_conn::initmysql_result(connection_pool *connPool){
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user")){
        //LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

void setnonblock(int fd){
    int flag = fcntl(fd , F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}
void addfd(int epfd, int fd, bool oneshot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(oneshot){
        event.events | EPOLLONESHOT;    //一个socket只触发一次
    }
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    setnonblock(fd);    //设置非阻塞
}
void removefd(int epfd, int fd){
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
void modfd(int epfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
}

void http_conn::init(){
    byte_have_send = 0;
    byte_to_send = 0; 
    m_check_state = CHECK_STATE_REQUESTLINE;    //初始化状态为解析请求行
    m_checkedidx = 0;
    m_startline = 0;
    m_readidx = 0;
    m_writeidx = 0;
    m_content_length = 0;
    m_method = GET;
    m_url = 0;
    m_boundary = 0;
    m_host = 0;
    m_version = 0;
    m_linger = false;
    m_fd = -1;
    tmpfd = -1;
    tmplen = 0;
    //m_content = 0;
    m_offset = 0;
    m_len = 0;
    //m_readbuf = new char[READ_BUFSIZE]();  // 使用new分配内存并初始化为0
    bzero(m_readbuf, READ_BUFSIZE);
    bzero(m_writebuf, WRITE_BUFSIZE);
    bzero(m_realfile, FILENAME_LEN);
}
void http_conn::init(int sockfd, sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addfd(m_epollfd, m_sockfd, true);
    m_user_count++;
    init();
}
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}
void http_conn::read_file(int fd1, int fd2){
    int pfd[2];
    pipe(pfd);
    ssize_t n;
    while (1) {
        // splice 从 fd1 到管道
        n = splice(fd1, NULL, pfd[1], NULL, BUF_SIZE, SPLICE_F_MOVE | SPLICE_F_MORE);
        if (n < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                printf("No data available, try later.\n");
                break;
            }
        }
        if (n == 0) {
            break;
        }
        if(tmpfd != -1){
            tmplen += n;
        }
        ssize_t left = n;
        while (left > 0) {
            // splice 从管道到fd2
            ssize_t written = splice(pfd[0], NULL, fd2, NULL, left, SPLICE_F_MOVE | SPLICE_F_MORE);
            if (written < 0) {
                if (errno == EINTR) continue;
            }
            left -= written;
        }
    }
    close(pfd[0]);
    close(pfd[1]);
    // 完成，返回临时文件 fd
    //return tmpfd;
}
bool http_conn::read(){
    //if(m_readidx >= READ_BUFSIZE){
    //    return false;
    //}
    //if(m_check_state == CHECK_STATE_CONTENT){ // strcmp(m_url, "/file") == 0 && m_method == POST){
        //printf("读请求体\n");
        //read_content();
        //return true;
    //}
    int byte_read = 0;
    while(1){
        if(m_readidx >= READ_BUFSIZE){
            if(tmpfd == -1){ // 创建临时文件
                char temp[] = "/home/ubuntu/cxd/webserver/resources/tmp/tmpfile_XXXXXX";
                tmpfd = mkstemp(temp);
                unlink(temp);
            }
            read_file(m_sockfd, tmpfd);
            return true;
        }
        byte_read = recv(m_sockfd, m_readbuf, READ_BUFSIZE - m_readidx, 0);
        if(byte_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                //printf("---dataover---\n");
                break;
            }
            else{
                return false;
            }
        } else if(byte_read == 0){
            return false;
        } 
        printf("fd :%d读位置:%d读取到数据：%s\n", m_sockfd, m_readidx, m_readbuf+m_readidx);
        m_readidx += byte_read;
    }
    return true;
}

http_conn::LINE_STATUS http_conn::parse_line(){   //解析一行(\r\n)
    char temp;
    for(; m_checkedidx < m_readidx; m_checkedidx++){
        temp = m_readbuf[m_checkedidx];
        if(temp == '\r'){
            if(m_checkedidx + 1 == m_readidx){  //\r为结尾字符 数据不完整
                return LINE_OPEN;
            } else if(m_readbuf[m_checkedidx + 1] == '\n'){
                m_readbuf[m_checkedidx++] = '\0';
                m_readbuf[m_checkedidx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if(temp == '\n'){
            if(m_checkedidx > 1 && m_readbuf[m_checkedidx - 1] == '\r'){
                m_readbuf[m_checkedidx-1] = '\0';
                m_readbuf[m_checkedidx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    printf("不完整的数据行\n");
    return LINE_OPEN;
}
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){ //获取请求方法、URL、http版本
    //GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");   //查找空格和\t
    if(!m_url){
        return BAD_REQUEST;
    }
    //GET\0/index.html HTTP/1.1
    *m_url++ = '\0';
    char* method = text;
    if (strcasecmp(method, "GET") == 0){ // 忽略大小写比较
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0){
        m_method = POST;
    } else{
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url," \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if(strcasecmp( m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }
    //http://192.168.1.1:8080/index.html
    if(strncasecmp(m_url, "http://", 7) == 0){   
        m_url += 7;
        m_url = strchr(m_url, '/');     //与strprbk类似
    }
    if(!m_url||m_url[0] != '/'){
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;     //状态转移
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;    //状态转移
            printf("状态转移CONTENT\n");
            return NO_REQUEST;
        }
        return GET_REQUEST;     //得到一个完整的请求(无请求体)
    } else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");    //返回空格和\t的数量
        if(strcasecmp(text, "close") == 0){
            m_linger = false;
        } else{
            m_linger =true;
        }
    } else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
        //m_content = new char[content_length+1]();
    } else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else if(strncasecmp(text, "Content-Type: multipart/form-data;", 34) == 0){
        //m_boundary = strstr(text, "boundary=");
        m_boundary = text;
        m_boundary += 44;
    }
    else{
        //printf("unknown headr %s \n", text);
    }
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if((m_readidx + tmplen) >= (m_content_length + m_checkedidx)){     //判断请求体是否被完整读入
        printf("数据完整,读指针：%d、分析指针：%d\n",m_readidx, m_checkedidx);
        text[m_content_length] = '\0';
        printf("数据为：%s\n", text);
        return GET_REQUEST;
    }
    printf("...数据不完整,读指针：%d、请求长度：%d\n",m_readidx,(m_content_length + m_checkedidx));
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request(){   //处理请求
    // 处理文件上传请求
    if (strcmp(m_url, "/file") == 0 && m_method == POST) {
        if (!m_boundary) {
            printf("未找到boundary\n");
            return BAD_REQUEST;
        }
        printf("边界：%s\n", m_boundary);
        char* file_part_start = strstr(m_readbuf + m_checkedidx, m_boundary);
        if (!file_part_start) {
            printf("未找到文件部分开始\n");
            return BAD_REQUEST;
        }
        // 查找文件名
        char* filename_start = strstr(file_part_start, "filename=\"");
        if (!filename_start) {
            printf("未找到filename\n");
            return BAD_REQUEST;
        }
        filename_start += 10; // 跳过 "filename=""
        char* filename_end = strchr(filename_start, '"');
        if (!filename_end) {
            printf("未找到filename结束标记\n");
            return BAD_REQUEST;
        }
        // 构建文件路径
        char filepath[FILENAME_LEN];
        snprintf(filepath, FILENAME_LEN, "/home/ubuntu/cxd/webserver/resources/file/%s", filename_start);
        int len = filename_end - filename_start;
        filepath[len + 42] = '\0';
        printf("文件路径: %s\n", filepath);
        // 查找文件内容的开始位置
        char* content_start = strstr(filename_end, "\r\n\r\n");
        if (!content_start) {
            printf("未找到文件内容开始位置\n");
            return BAD_REQUEST;
        }
        content_start += 4; // 跳过 \r\n\r\n
        // 查找文件内容的结束位置
        char* content_end = strstr(content_start, m_boundary);
        if (!content_end) {
            content_end = m_readbuf + m_readidx;
        }
        else{
            content_end -= 2;
        }
        printf("文件内容长度: %ld\n", content_end - content_start);
        int fd = open(filepath, O_WRONLY | O_CREAT, 0644);
        ::write(fd, content_start, content_end - content_start);
        if(tmpfd != -1){
            //拼接临时文件
            lseek(tmpfd, 0, SEEK_SET);
            read_file(tmpfd, fd);
        }
        close(tmpfd);
        tmpfd = -1;
        close(fd);
        // 返回成功响应
        add_status_line(200, ok_200_title);
        add_headers(0);
        return FILE_REQUEST;
    }
    //处理登录注册请求
    if((strcmp(m_url, "/login") == 0 || strcmp(m_url, "/register") == 0 )&& m_method == POST){
        char name[100]="";
        char *password = 0;
        int i = m_checkedidx + 5;
        for(; m_readbuf[i]!='&'; i++){
            name[i - m_checkedidx - 5] = m_readbuf[i];
        }
        password = m_readbuf + i + 10;
        printf("用户名 %s,密码 %s\n",name, password);
        if(strcmp(m_url, "/login") == 0){
            if(users.find(name) != users.end() && users[name] == password){
                printf("密码正确\n");
                strcpy(m_url, "/1.html");
            }else{
                printf("密码错误\n");
                strcpy(m_url, "/logerror.html");
            }
        }else{
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            if (users.find(name) == users.end()){
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();
                if (!res)
                    strcpy(m_url, "/index.html");
                else
                    strcpy(m_url, "/regerror.html");
            }
            else{
                strcpy(m_url, "/regerror.html");
            }
        }
    }
    // 处理文件删除请求
    if (strncmp(m_url, "/delete", 7) == 0) {
        strcpy(m_realfile, doc_root);
        int len =strlen(doc_root);
        strncpy(m_realfile + len, m_url + 7, FILENAME_LEN - len - 1);
        if (unlink(m_realfile) == 0) {
            add_status_line(200, ok_200_title);
            add_headers(0);
            return FILE_REQUEST;
        } else {
            return INTERNAL_ERROR;
        }
    }
    // 处理文件列表请求
    if (strcmp(m_url, "/file/list") == 0) {
        char file_list[WRITE_BUFSIZE] = "";
        int pos = 0;
        DIR* dir = opendir("/home/ubuntu/cxd/webserver/resources/file");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_REG) {  // 只处理普通文件
                    strcpy(file_list + pos, entry->d_name);
                    pos += strlen(entry->d_name);
                    file_list[pos++] = '\n';
                }
            }
            closedir(dir);
        }
        file_list[pos] = '\0';
        add_status_line(200, ok_200_title);
        add_content_type();
        add_headers(pos);
        add_content(file_list);
        return FILE_REQUEST;
    }
    //// 处理文件请求"/home/ubuntu/cxd/webserver/resources"
    strcpy(m_realfile, doc_root);
    int len =strlen(doc_root);
    strncpy(m_realfile + len, m_url, FILENAME_LEN - len - 1);
    if(stat(m_realfile, &m_filestat) < 0){
        return NO_RESOURCE;
    }
    if(!(m_filestat.st_mode & S_IROTH)){    //判断访问权限
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_filestat.st_mode)){    //判断是否是目录
        return BAD_REQUEST;
    }
    printf("打开文件描述符\n");
    m_fd = open(m_realfile, O_RDONLY);
    m_len = m_filestat.st_size;
    return FILE_REQUEST;
}
void http_conn::unmap() {
    if(m_fileaddr){
        munmap(m_fileaddr, m_filestat.st_size);
        m_fileaddr = 0;
    }
}
http_conn::HTTP_CODE http_conn::process_read(){    //主状态机
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
    || ((m_check_state != CHECK_STATE_CONTENT) && (line_status = parse_line()) == LINE_OK)){    //解析到一行完整数据 或 解析到了请求体
        text = get_line();
        m_startline = m_checkedidx;
        //printf( "检查指针：%d Got 1 http line: %s\n", m_checkedidx,text);
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return ret;
                }
                break;
            case CHECK_STATE_HEADER:
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return ret;
                } else if(ret == GET_REQUEST){
                    return do_request();
                }
                break;
            case CHECK_STATE_CONTENT:
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

bool http_conn::write(){
    int temp = 0;
    int ret = 0;
    if(byte_to_send <= 0 && m_fd == -1){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();     //此次响应结束
        return true;
    }
    while(1){
        if(byte_to_send > 0){
            //tcp延迟发送
            temp = send(m_sockfd, m_writebuf + byte_have_send, m_writeidx, MSG_MORE);
            if(temp == -1){
                if(errno == EAGAIN){
                    modfd(m_epollfd, m_sockfd, EPOLLOUT);
                    return true;
                }
                return false;
            }
            byte_to_send -= temp;
            byte_have_send += temp;
        }
        if(byte_to_send <= 0 && m_fd != -1){  //头部发送完毕
            if(m_fd != -1){
                ret = sendfile(m_sockfd, m_fd, &m_offset, m_len);
                if(ret == -1){
                    if(errno == EAGAIN){
                    modfd(m_epollfd, m_sockfd, EPOLLOUT);
                    return true;
                    }
                    close(m_fd);
                    m_fd = -1;
                    return false;
                }
                m_len -= ret;
                printf("已发送%d字节\n",ret);
            }
        }
        if(byte_to_send <= 0 && m_len<= 0){  //所有数据发送完毕
            printf("发送完毕\n");
            if(m_fd != -1){
                close(m_fd);
                m_fd = -1;
            }
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if(m_linger){
                init();
                return true;
            } else{
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char* format, ...){
    if(m_writeidx >= WRITE_BUFSIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_writebuf + m_writeidx, WRITE_BUFSIZE - 1 - m_writeidx, format, arg_list);
    if(len >= (WRITE_BUFSIZE - 1 - m_writeidx)){
        va_end(arg_list);
        return false;
    }
    m_writeidx += len;
    va_end(arg_list);
    return true;
}
bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    //add_content_type();
    add_linger();
    add_blank_line();
    printf("响应头：%s\n", m_writebuf);
}
bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n", content_len);
}
bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char* content){
    return add_response( "%s", content);
}
bool http_conn::add_content_type(){
    if (strcmp(m_url, "/file/list") == 0){
        return add_response("Content-Type: text/plain\r\n");
    }
    return add_response("Content-Type: text/html\r\n");
}

bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)){
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)){
                return false;
            }
            break;
        case FILE_REQUEST:
            if(strcmp(m_url, "/file/list") == 0 || (strcmp(m_url, "/file") == 0 && m_method == POST) || strncmp(m_url, "/delete", 7) == 0){
                break;
            }
            add_status_line(200, ok_200_title);
            add_headers(m_filestat.st_size);
            byte_to_send = m_writeidx;
            return true;
        default:
            return false;
    }
    byte_to_send = m_writeidx;
    return true;
}

void http_conn::process(){     //线程池工作线程调用，处理HTTP请求
    //解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    //生成响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        printf("fd %d 关闭连接\n", m_sockfd);
       close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}