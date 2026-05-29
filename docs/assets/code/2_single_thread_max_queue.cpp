#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cassert>
#include <arpa/inet.h>
#include <unistd.h>

void set_socket_to_listen(int s){
    struct sockaddr_in addr;
    addr.sin_family      = AF_INET; // use IPv4 or IPv6
    addr.sin_port        = htons(8080); // set port 8080
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // set wildcard matching on IP address. 
    // This means that the kernel will listen to all IP addresses that this machine owns.
    
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes); // free the socket if its being used by someone else.

    int b = bind(s, (struct sockaddr*)&addr, sizeof addr);
    if (b < 0) { perror("bind"); exit(1); }
    
    int l = listen(s, 4096); // accept atmost 4096 connections into the queue. Drop the rest.
    if (l < 0) { perror("listen"); exit(1); }
}

int accept_from_queue_and_return_fd(int s){
    struct sockaddr_storage their_addr;
    socklen_t addr_size = sizeof their_addr;
    return accept(s, (struct sockaddr*)&their_addr, &addr_size);
}

ssize_t perform_logic_and_populate_response(const char *req_buf, int req_size, char *res_buf, int res_size){
    int num = atoi(req_buf); // atoi will default to 0 on bad char*.

    if(num <= 0) return snprintf(res_buf, res_size, "Invalid\n");
    if(num & 1)return snprintf(res_buf, res_size, "Odd\n"); 
    else return snprintf(res_buf, res_size, "Even\n");
}

int main(){
    // INITIALIZE A SOCKET.
    int s = socket(AF_INET, SOCK_STREAM, 0); 
    if (s < 0) { perror("socket"); return 1; }

    // SETUP THE SOCKET TO LISTEN FOR TCP connections.
    set_socket_to_listen(s);

    while(true){
        // ESTABLISH CONNECTION.
        int fd = accept_from_queue_and_return_fd(s);
        if(fd < 0) continue; // failed to establish connection.

        struct timeval tv = {2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));  // set a timeout of 2s per recv call. 

        char req_buf[20], res_buf[20];

        // RECEIVE REQUEST
        ssize_t num_bytes_read = recv(fd, req_buf, sizeof req_buf - 1, 0);
        if(num_bytes_read <= 0){
            close(fd);
            continue; // failed to recieve a request.
        }
        req_buf[num_bytes_read] = '\0';

        // PERFORM LOGIC.
        ssize_t num_bytes_written = perform_logic_and_populate_response(req_buf, num_bytes_read, res_buf, sizeof res_buf);

        // SEND RESPONSE.
        ssize_t num_bytes_sent = send(fd, res_buf, num_bytes_written, 0);
        
        // CLOSE CONNECTION.
        close(fd); // close the connection.
    }

    return 0;
}