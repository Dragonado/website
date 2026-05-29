#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cassert>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <csignal>

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

// Flip an fd to O_NONBLOCK so recv/send/accept return EAGAIN instead of blocking.
void make_nonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { perror("fcntl F_GETFL"); exit(1); }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL O_NONBLOCK"); exit(1);
    }
}

// Per-connection state. Lives on the heap from accept to close, key is data.ptr.
struct connection {
    int fd;

    char req_buf[20];
    size_t req_bytes;   // bytes accumulated so far (waiting for '\n')

    char res_buf[20];
    size_t res_len;     // total response length once we have one
    size_t res_sent;    // bytes of the response already sent

    enum { READING_REQUEST, WRITING_RESPONSE } state;
};

// Remove from epoll, close the fd, free the state. After this, c is invalid.
void close_connection(int ep_fd, connection *c){
    epoll_ctl(ep_fd, EPOLL_CTL_DEL, c->fd, nullptr);
    close(c->fd);
    delete c;
}

// Non-blocking write loop. Sends from c->res_buf[c->res_sent..c->res_len]. If
// the kernel send buffer fills (EAGAIN), state is preserved in c and we'll be
// re-invoked when EPOLLOUT fires again. If fully sent, the connection closes.
void do_write(int ep_fd, connection *c){
    while (c->res_sent < c->res_len) {
        ssize_t n = send(c->fd, // file descriptor
                         c->res_buf + c->res_sent, // buffer offset
                         c->res_len - c->res_sent, // len remaining
                         MSG_NOSIGNAL);    // avoid SIGPIPE on dead peers
        // Successfully written
        if (n > 0) { c->res_sent += n; continue; }
        // non blocking error. Come back later.
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        // Interrupted by kernel, so try again.
        if (n < 0 && errno == EINTR) continue;
        // real error
        close_connection(ep_fd, c);
        return;
    }
    // fully sent
    close_connection(ep_fd, c);
}

// Non-blocking read loop. Accumulates bytes into c->req_buf until we see '\n'.
// On finding '\n': parse, format response, switch to write mode, and try to
// send opportunistically (often completes in one shot). On EAGAIN: return,
// state is preserved.
void do_read(int ep_fd, connection *c){
    while (true) {
        ssize_t n = recv(c->fd,
                         c->req_buf + c->req_bytes,
                         sizeof(c->req_buf) - 1 - c->req_bytes,
                         0);
        if (n > 0) {
            c->req_bytes += n;

            // Complete message.
            char *nl = (char *)memchr(c->req_buf, '\n', c->req_bytes);
            if (nl != nullptr) {
                *nl = '\0';   // null-terminate so atoi is safe
                c->res_len = perform_logic_and_populate_response(
                    c->req_buf, (int)c->req_bytes, c->res_buf, (int)sizeof(c->res_buf));
                c->res_sent = 0;
                c->state = connection::WRITING_RESPONSE;

                // Switch our epoll interest from EPOLLIN to EPOLLOUT for this fd.
                struct epoll_event ev;
                ev.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
                ev.data.ptr = c;
                if (epoll_ctl(ep_fd, EPOLL_CTL_MOD, c->fd, &ev) < 0) {
                    perror("epoll_ctl MOD to OUT");
                    close_connection(ep_fd, c);
                    return;
                }

                // Try sending right away instead of getting it from epoll_wait.
                do_write(ep_fd, c);
                return;
            }

            // No newline yet AND buffer full. client misbehaving, drop it.
            if (c->req_bytes >= sizeof(c->req_buf) - 1) {
                close_connection(ep_fd, c);
                return;
            }

            // keep recv'ing
            continue;
        }
        if (n == 0) {                                            // peer closed
            close_connection(ep_fd, c);
            return;
        }
        // Not ready to recv. Come back later.
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        // Interrupted by kernel, try again.
        if (errno == EINTR) continue;
        close_connection(ep_fd, c);                              // real error
        return;
    }
}

// Drain the accept queue. Listener is non-blocking, so accept() returns EAGAIN
// when no more connections are pending. For every new connection: allocate
// state, make non-blocking, register with epoll (EPOLLIN | EPOLLET).
void accept_new_connection(int ep_fd, int s_fd){
    while (true) {
        int conn_fd = accept_from_queue_and_return_fd(s_fd);
        if (conn_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;   // queue drained
            perror("accept");
            return;
        }
        make_nonblocking(conn_fd);

        connection *c = new connection;
        c->fd = conn_fd;
        c->req_bytes = 0;
        c->res_len = 0;
        c->res_sent = 0;
        c->state = connection::READING_REQUEST;

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.ptr = c;
        if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, conn_fd, &ev) < 0) {
            perror("epoll_ctl ADD client");
            close(conn_fd);
            delete c;
            continue;
        }
    }
}

int main(){
    signal(SIGPIPE, SIG_IGN);

    // INITIALIZE A SOCKET.
    int s_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_fd < 0) { perror("socket"); return 1; }

    // SETUP THE SOCKET TO LISTEN FOR TCP connections.
    set_socket_and_listen(s_fd);

    // Listener must be non-blocking so accept_new_connection can loop to EAGAIN.
    make_nonblocking(s_fd);

    #define MAX_EPOLL_EVENTS 100
    struct epoll_event events[MAX_EPOLL_EVENTS];

    // Create Epoll file descriptor.
    int ep_fd = epoll_create1(0);
    if (ep_fd < 0) { perror("epoll_create1"); return 1; }

    // Register the listener. Sentinel: data.ptr == nullptr means "this is the listener".
    struct epoll_event ev;
    ev.events = EPOLLIN;       // LT on listener — simpler, listener has only one event source
    ev.data.ptr = nullptr;
    if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, s_fd, &ev) < 0) {
        perror("epoll_ctl ADD listener");
        return 1;
    }

    while (true) {
        int n = epoll_wait(ep_fd, events, MAX_EPOLL_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            return 1;
        }

        for (int i = 0; i < n; i++) {
            uint32_t flags = events[i].events;

            // Socket connection given by epoll, that means we are ready to accept new conn.
            if (events[i].data.ptr == nullptr) {
                accept_new_connection(ep_fd, s_fd);
                continue;
            }
            
            // Event connection give by epoll, that means we need to read or write from this.
            connection *c = (connection *)events[i].data.ptr;

            // Error or hangup. Just drop it.
            if (flags & (EPOLLERR | EPOLLHUP)) {
                close_connection(ep_fd, c);
                continue;
            }

            if (flags & EPOLLIN) {
                do_read(ep_fd, c);
                continue;
            }

            if (flags & EPOLLOUT) {
                do_write(ep_fd, c);
            }
        }
    }

    return 0;
}
