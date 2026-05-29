#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cassert>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>

void set_socket_and_listen(int s_fd){
    struct sockaddr_in addr;
    addr.sin_family      = AF_INET; // use IPv4 or IPv6
    addr.sin_port        = htons(8080); // set port 8080
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // set wildcard matching on IP address. 
    // This means that the kernel will listen to all IP addresses that this machine owns.
    
    int yes = 1;
    setsockopt(s_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes); // free the socket if its being used by someone else.

    int b = bind(s_fd, (struct sockaddr*)&addr, sizeof addr);
    if (b < 0) { perror("bind"); exit(1); }
    
    int l = listen(s_fd, 4096); // accept atmost 4096 connections into the queue. Drop the rest.
    if (l < 0) { perror("listen"); exit(1); }
}

int accept_from_queue_and_return_fd(int s_fd){
    struct sockaddr_storage their_addr;
    socklen_t addr_size = sizeof their_addr;
    return accept(s_fd, (struct sockaddr*)&their_addr, &addr_size);
}

ssize_t perform_logic_and_populate_response(const char *req_buf, int req_size, char *res_buf, int res_size){
    int num = atoi(req_buf); // atoi will default to 0 on bad char*.

    if(num <= 0) return snprintf(res_buf, res_size, "Invalid\n");
    if(num & 1)return snprintf(res_buf, res_size, "Odd\n"); 
    else return snprintf(res_buf, res_size, "Even\n");
}

int main(){
    // INITIALIZE A SOCKET.
    int s_fd = socket(AF_INET, SOCK_STREAM, 0); 
    if (s_fd < 0) { perror("socket"); return 1; }

    // SETUP THE SOCKET TO LISTEN FOR TCP connections.
    set_socket_and_listen(s_fd);

    #define MAX_EPOLL_EVENTS 100
    struct epoll_event events[MAX_EPOLL_EVENTS];

    // Create Epoll file descriptor.
    int ep_fd = epoll_create1(0);

    // Default event.
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = s_fd;

    if(epoll_ctl(ep_fd, EPOLL_CTL_ADD, s_fd, &ev) < 0){
        perror("epoll");
        return 1;
    }

    while(true){
        // ESTABLISH CONNECTION.
        int num_active_conn = epoll_wait(ep_fd, events, MAX_EPOLL_EVENTS, 1 * 1000);

        for(int i = 0; i < num_active_conn; i++){ 
            if(events[i].data.fd == s_fd){
                int conn_fd = accept_from_queue_and_return_fd(s_fd);
                if(conn_fd < 0) continue; // failed to establish connection.
                // setnonblocking(conn_fd); I dont understand this yet.
                ev.events = EPOLLIN;
                ev.data.fd = conn_fd;
                if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(EXIT_FAILURE);
                }
                struct timeval tv = {2, 0};
                setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));  // set a timeout of 2s per recv call. 
            }
            else{
                int conn_fd = events[i].data.fd;
                if (events[i].events & (EPOLLERR | EPOLLHUP)) { close(conn_fd); continue; }

                char req_buf[20], res_buf[20];

                // RECEIVE REQUEST
                ssize_t num_bytes_read = recv(conn_fd, req_buf, sizeof req_buf - 1, 0);
                if(num_bytes_read <= 0){
                    close(conn_fd);
                    continue; // failed to recieve a request.
                }
                req_buf[num_bytes_read] = '\0';

                // PERFORM LOGIC.
                ssize_t num_bytes_written = perform_logic_and_populate_response(req_buf, num_bytes_read, res_buf, sizeof res_buf);

                // SEND RESPONSE.
                ssize_t num_bytes_sent = send(conn_fd, res_buf, num_bytes_written, 0);
                
                // CLOSE CONNECTION.
                close(conn_fd); // close the connection.
            }
        }
    }

    return 0;
}
