---
date: 2026-05-15
---

# The simple task of hosting an API


## Motivation

A few weeks ago, a friend of mine asked me "@Chai, do you have an idle server running that I can ssh into? I want to host an experimental server that people can query for a course project."

I was so mind-boggled with this question. Is this such a casual question to ask someone? Do students in this college just have idle servers running? Suppose I straight up gave access to my MacBook to host his server, how does one even create a server from scratch and serve others?

I played it off cool and said "Nah bro, I don't have a server right now" to which he just said "Cool, I'll just rent an AWS box then. There should be a free trial available".

This was the end of the conversation but it stuck with me for quite a while. As a grad student at Georgia Tech in CS, I should not be stumped with this simple question, so I decided to learn a little about networks and write this blog on "**the simple task of hosting an API**".

Also, my friend is joining Jane Street. So maybe it is a casual question to ask for someone like him ¯\\\_(ツ)\_/¯


## Background

I used to work at Google as a SWE before, and I always took infra work like hosting an API for granted. If I needed to serve a service to someone in Google, all I had to do was spin up a [Boq node](https://www.reddit.com/r/programming/comments/s21wti/comment/hsculqs/) (the internal microservice framework) and all the boilerplate work would be taken care of. I just needed to define the IO parameters, write the service part, and send the endpoint (provided by the internal load balancer) to my colleague who wanted the service.

Of course it's only this simple for experimental work. For production grade services an engineer has to ensure the protobufs match the client requirements + debugging information, add unit tests, regression tests, end-to-end tests, functional tests, load tests, analyze user behaviour, analyze system usage, etc.

Joining Google Ads straight out of undergrad was an opportunity that I will forever be grateful for. I had immense growth both professionally and personally. I learnt a great deal of software engineering and how production code is supposed to be shipped that can handle planet-scale. So while I learnt a great deal of **using** planet-scale infra, I was severely lacking in technical abilities of actually **building** a planet-scale infra. I came away with a lot: strong software engineering habits, real fluency in writing documentation, deep domain knowledge of Ads, and good intuition for how users and advertisers actually behave. What I didn't come away with was experience on the deep technical layer like improving the load balancer, shaving boot-time latency of Borg machines, that kind of work.

So I joined back academia. Case in point, here is a [blog post](https://mishal23.github.io/back-to-academia/) from another Google engineer (and friend) about going back to academia. We share the same sentiment but reached different conclusions. He chose to stay in Google and pursued an online masters from Georgia Tech part time. I chose to leave Google and pursue an in-person masters degree from Georgia Tech full time. Note to the reader: there is no right/wrong answer in our decisions here.

## Hardware

The easiest solution was to input my credit card into one of the cloud providers (AWS/GCP/Oracle) and get an instance of one of their machines that I can ssh into. Basically what my friend ended up doing. But I don't want to do that for several reasons. 

So what I'm gonna do is use my fully decked-out Raspberry Pi 5 that has been sitting idle in my closet for the past 3 years. This Pi comes with a cooling fan + 128 GB SD storage + 2.4GHz quad-core Arm Cortex-A76 + 8GB RAM running the Linux-based Raspberry Pi OS.

Looks something like this before assembling:

![](https://www.canakit.com/Media/700/2915.jpg)

This is the machine that will be hosting my server. I will be using my MacBook M1 Air that acts as a client querying the server.

## Setup

### Big picture

![](../../assets/api-setup-network-diagram.jpg)

As mentioned before, I will be using my Pi as the server and I will be editing the Pi files on my Mac via SSH. I will also be using my Mac to act as a client to ping the server. So there are two paths from my Mac to the Pi.

One thing to note is that my ISP doesn't provide a global static IP address. So for now I can only access this server from the **devices connected to my Wi-Fi**. If I want to host it on the open internet then I would also need to disable my firewalls and expose my ports. I am smart enough to know that I'm not yet skilled in cybersec to play with these configurations, so I shall not venture into this yet.

Before running the server, we need to get the machine running first. The Canakit version I bought comes with the Raspberry Pi OS pre-loaded, so I really don't have to do anything here. All I need to do is turn it on and enable ssh on this machine.

```bash
raspberrypi:~ $ sudo systemctl enable --now ssh
```

And now I ssh into the machine from my mac.

```zsh
MacBook-Air ~ % ssh chaithu@raspberrypi.local
```

Wait, I can just do `chaithu@raspberrypi.local` instead of inputting an IP address?

### mDNS

IP addresses provided by my Wi-Fi access point are not really static. The DHCP server in my home router hands out a lease that typically expires every 24 hours, and clients renew it (usually getting the same IP back, but not guaranteed). The IP can change when the lease expires while the device is off, when the router reboots, or when DHCP gets confused. Even though it's a slow refresh, it's a bit of a pain to find out the Pi's IP address each time, so I just use **mDNS**. I'm sure nothing can go wrong with this simple hack. (_subtle foreshadowing_)

mDNS is a really neat protocol for resolving hostnames to IP addresses of devices in a local network. Basically my Mac sends a multicast query to all the devices in my network. My Pi sees this call and responds with a similar multicast packet containing the IP address it owns.

```zsh
MacBook-Air ~ % ping -q -c 10 raspberrypi.local
PING raspberrypi.local (192.168.1.24): 56 data bytes

--- raspberrypi.local ping statistics ---
10 packets transmitted, 10 packets received, 0.0% packet loss
round-trip min/avg/max/stddev = 16.264/30.368/52.540/12.168 ms
```

We can see that `raspberrypi.local` resolves to `192.168.1.24` and a ping takes ~30ms on average.

### Server Code

Now this is the most fun part! Writing the code for the server. For maximum learning I decided to use C++. I would use pure C but multithreading in C is an absolute pain point and I would much rather deal with `std::thread` for this project. However, I didn't end up using multithreading for this project.

Let's start with a very simple bare-bones server. I won't explain most of the code since it's basic stuff. I highly recommend reading the [Beej networking guide](https://beej.us/guide/bgnet/) to learn the basics. It's a really good tutorial after which I went from 0 to a basic socket programmer that can understand the man pages of the network syscalls.

```cpp
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

    int l = listen(s, 1); // accept atmost 1 connection into the queue. Drop the rest.
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
    if(num & 1) return snprintf(res_buf, res_size, "Odd\n");
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
```

Some things to note:

- Server is running on port 8080.
- Server accepts at most 1 connection in its queue as seen on the `listen()` command.
- All the API is doing is returning whether the given number is Odd or Even, with some basic error handling.

Let's try to run it on my Pi.

```bash
raspberrypi:~/Desktop/server $ g++ single_thread.cpp -o server && ./server
```

and on my Mac I query the server.

```zsh
MacBook-Air ~ % echo "100" | nc raspberrypi.local 8080
Even
```

Absolute success! Most of it is basic boilerplate code. If you want to change the API, literally the only thing that needs to be changed is the `perform_logic_and_populate_response` function.

Also a neat thing to notice is that this communication is completely unencrypted (you might have observed this in my diagram). So anyone inside my Wi-Fi network can snoop around and intercept this communication with a simple `tcpdump` command.

## Stress test

Sure, technically this is a valid server API that people can now query for their needs. But how good is it? How good can we make it? What does "good" even mean?

There are many measures of what a good server should be. For example you can optimise for metrics like:

- Throughput: how many queries can the server respond to?
- Latency: how long does it take to get a response?
- Reliability: does the server have any downtime?
- CPU usage
- Memory usage
- etc.

For now, I only care about throughput and latency. This setup is pretty bad reliability-wise because it's literally just one machine that could go off at any time. CPU and memory are anyway a physical limitation that I can't really change.

### Baseline

I asked Claude to make a stress test script using python. So with this I can basically control the QPS (queries per second) and then measure the success rate and the latencies.

You can find it on my github [here](https://github.com/Dragonado/IPServer/blob/main/stress.py).

[TBA Claude] update setup.
The setup is: I send a linearly increasing load for the first few seconds to eliminate any cold start issues occurring on the client, network, or the server side. I then maintain a steady state at the given QPS limit for the given duration.

Latency numbers are only taken from the steady state phase but I still chart the entire load.

Here is a basic sanity check of the script:

```zsh
(.venv) MacBook-Air IPServer % python3 stress.py --host raspberrypi.local --qps 10 --warmup 1 --steady 5

[client] target raspberrypi.local:8080  qps=10  warmup=1.0s  steady=5.0s  timeout=15.0s
[client] expected attempts: ~5 (warmup) + 50 (steady) = 55
[client] log: stress_log.jsonl
[client] schedule: 5 warmup + 50 steady = 55 total
[client] query mode: varying — payload = 1 + (rid-1) % 1000, expected = Odd/Even per parity
[client] issued 55 requests in 5.90s (target 6s); awaiting in-flight...
[client] all in-flight complete at 6.59s
[client] wrote 55 records to stress_log.jsonl

=== Warmup phase (n=5) ===
  Correct  :       5  (100.0%)
  Wrong    :       0  (  0.0%)
  Errors   :       0  (  0.0%)
    ok                                                                 5  (100.0%)

=== Steady phase (n=50) ===
  Correct  :      50  (100.0%)
  Wrong    :       0  (  0.0%)
  Errors   :       0  (  0.0%)
    ok                                                                50  (100.0%)

=== Latency — steady phase, anchored to t_scheduled (n=50) ===
metric                                   count       min       avg       p50       p90       p99     p99.9       max
connect_ms  (scheduled -> connected)        50   114.124   230.030   187.249   398.705   642.742   685.527   690.281
ttfb_ms     (scheduled -> 1st byte)         50   135.634   299.521   211.728   589.575   930.229   974.246   979.137
total_ms    (scheduled -> close)            50   135.743   320.891   211.909   689.777  1121.291  1201.245  1210.129
queue_ms    (scheduled -> start)            50     0.142     1.170     1.239     1.440     2.386     2.707     2.743

  queue_ms is the client-side delay: how late the coroutine started vs its
  scheduled time. Should be ~0 when not saturated; growing means coordination
  omission is happening and tail latencies are real (not artifacts).

Achieved issue rate (steady phase): 10.2 q/s over 4.90s
[client] wrote chart to stress_chart.png
```

[TBA Claude] add the correct chart and update numbers,
Yay, looks like everything works correctly. Average latency is 300ms whereas 99% of the packets complete within 1.1s.

This is a good baseline.

!!! info "What these latency numbers actually measure" 
These latency numbers basically measure the network and socket handling. The actual API itself (code inside the `while` loop) is super fast. All it does is receive 20 bytes of data, do a few operations and branches (deciding if the number is odd/even), and then send 20 bytes of data. All this is in the order of microseconds and hence not a bottleneck for our case.

![Baseline stress chart: 10 QPS for 5 seconds against the single-threaded server](../../assets/stress_chart_baseline.png)

### Optimisations

#### Increasing the Accept Queue size because someone attacked my server

##### The attack

Let's just run the sanity check again.

```zsh
(.venv) MacBook-Air IPServer % python3 stress.py --host raspberrypi.local --qps 10 --warmup 1 --steady 1

[client] target raspberrypi.local:8080  qps=10  warmup=1.0s  steady=1.0s  timeout=15.0s
[client] expected attempts: ~5 (warmup) + 10 (steady) = 15

=== Warmup phase (n=5) ===
  Correct  :       0  (  0.0%)
  Wrong    :       0  (  0.0%)
  Errors   :       5  (100.0%)
    timeout                                                            5  (100.0%)

=== Steady phase (n=10) ===
  Correct  :       0  (  0.0%)
  Wrong    :       0  (  0.0%)
  Errors   :      10  (100.0%)
    timeout                                                           10  (100.0%)
```

Whaaaaat? Why is everything failing? My Pi is working completely fine and there was no downtime. The server is still running, I literally queried it a bunch of times just before running the above stress test. I haven't rebuilt the binary or anything. And I'm just running a small stress test (10 QPS) so it's not a performance issue. Yet, the queries are timing out.

#### What was it?
Can you guess why? It's a not-so-popular cybersecurity attack that accidentally happened here due to my bad coding configurations. No, it's not DDoS but close.

<details>
<summary><b>Click to reveal the answer</b></summary>

It's the [Slowloris attack](https://en.wikipedia.org/wiki/Slowloris_(cyber_attack)) — or more technically, a Slowloris-*like* attack, since the classic Slowloris works on HTTP requests whereas my issue is at the TCP layer.

</details>

It's basically a type of [slow DoS attack](https://en.wikipedia.org/wiki/Slow_DoS_attack) where a malicious user causes unavailability of the service by sending partial requests to keep the connection alive and reducing the server's connection capacity. Obviously mine wasn't a malicious user but an accidental occurrence of this attack.

#### What happened?

My current implementation of the server tells the kernel to only keep an accept queue of size `1` as seen on my `listen(fd, 1)` command. So any connections arriving after this queue is full are silently dropped by the kernel. This is what's happening here — my server has a full queue and is unable to accept any more requests.

#### Why did it happen?

Why is my queue full though? Shouldn't my server just `accept()` the connection and serve it and free the queue?

<details>
<summary><b>Click to reveal the answer</b></summary>

It's because my beautiful single-threaded program is stuck on the blocking `recv()` call and cannot move on. I mentioned that I ran a bunch of queries before running this one and I didn't rebuild the binary, right? Turns out that one of the queries did not behave correctly.

</details>

It looks like one of the queries, after establishing a connection with my Pi, never sent its request bytes (due to Wi-Fi packet loss maybe? it happens a lot so not surprised) and so my Pi is keeping the connection alive in hopes of getting a request back. This is the Slowloris-like attack that happened to me :'(.

Unfortunately, [TCP keep-alive](https://tldp.org/HOWTO/TCP-Keepalive-HOWTO/overview.html) is off by default, so the server would be held hostage forever by one bad packet loss. While turning on TCP keep-alive would eventually clean up connections lost to packet loss, a malicious actor could still bypass it.

I would have had a very hard time debugging this flaky issue without Claude. It gave me a bunch of things I could try and I was able to diagnose this and confirm that this is indeed the case using the `ss` (socket statistics) command to inspect the socket connections on port `8080`.

The proper fix is to make `recv` non-blocking using the `epoll` syscall. With `epoll` we can choose to only handle connections that are actually active.

Workaround for now:

- Kill the server.
- Increase the queue size to the system maximum (which is `4096` on my Pi, set by `/proc/sys/net/core/somaxconn`).
- Set a timeout on the `recv()` syscall via `SO_RCVTIMEO` (stopgap measure for now instead of `epoll`).
- Rebuild the binary.
- Start the server again.

Increasing the accept queue size increases the throughput of the server because we can answer more queries by queuing them instead of dropping them. However, this doesn't change the number of queries we can respond to **per second** because we aren't any faster. The queue is a shock absorber, not a speed-up.

[TBA: add correct numbers] This gives us around 80 QPS at the same 1.1s p99 latency:

![Stress chart for the single-threaded server with a 4096 backlog](../../assets/stress_chart_single_thread_80.png)

To make our server answer more queries per second, we have to add some non-blocking, because right now we are making our thread sit idle in `recv` waiting for one particular connection's bytes when there might be another connection with its request bytes already ready and waiting to be served.

#### Epoll

`epoll` is Linux's mechanism for asking the kernel "here's a bunch of file descriptors I care about and wake me when *any* of them have something I can do."

There are two ways to use `epoll`, and the difference is more important than it looks.

#### Epoll + blocking I/O

My first version of the epoll server was me trying to code it after reading the official epoll documentation.


```cpp
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
```
Yes I'm techinically using epoll and the code should run correctly but I'm not leveraging epoll correctly. I seem to have added some concurrency here but is it correct? 

<details>
<summary><b>Click to reveal the answer</b></summary>

Absolutely not! because if you notice, I'm still fully blocked on `recv` and `send`. My single thread needs to wait idle until `recv` is ready to recieve all its bytes. So I'm interacting with exactly 1 connection at a time from the connection start to connection end. This is **NOT** concurrency.

</details>

What I just did:

- Get a connection from `epoll`.
- Open the connection. 
- Serve the connection.
- Close the connection.
- Repeat.

[TBA insert flowchart]

What I should be doing:

- Get a connection from epoll.
- Open the connection.
- Serve the connection as much as I can.
- The moment I become idle, save the state of the connection and put it back on the epoll.
- Since I'm idle again, I'm free to work the next connection I get from the epoll.
- If the connection I served is complete, close the connection.
- Repeat.

[TBA insert flowchart]

Clearly, using `epoll` is more complicated than I thought but the correct usage allows me to save the state of a connection and do partial progress and work on other stuff concurrently and not sit idle. This is where the massive potential for gains awaits.

What I did here is just add a fancier selection algorithm on getting which connection to interact with but the underlying concurrency model is still "one request at a time, serially."

This is a real anti-pattern: it *looks* like I'm doing some fancy concurrency stuff but it's just fluff and no real advantage because I'm using it wrongly.

This compiles, runs, and handles around 100 QPS which is exactly the same as before.

[TBA insert image of chart].

#### Epoll + non-blocking I/O

The proper version puts every file descriptor into non-blocking mode via `fcntl(fd, F_SETFL, O_NONBLOCK)` and refactors the connection handler into a **state machine** that lives on the heap, keyed by the file descriptor. Each connection has a struct holding its read buffer, write buffer, byte counters, and current state (`READING_REQUEST` vs `WRITING_RESPONSE`).

The flow becomes:

1. `epoll_wait` returns "fd X has an event."
2. Look up the per-connection state via `data.ptr` (the kernel hands you back whatever pointer you stashed when you registered the fd — no hash table lookup needed).
3. Do whatever work you can — `recv` bytes, parse if you've seen `\n`, `send` bytes if you have a response — and on `EAGAIN`, return immediately. The thread is now free to handle other connections.
4. When the kernel has more to tell us about this fd, `epoll_wait` will return it again and we resume the state machine where we left off.

The key insight: with blocking I/O, a single connection's network round-trips are *serialized* on the thread. With non-blocking I/O + epoll, the kernel runs each connection's network work *in parallel* underneath, and the thread only does the userspace bits when they're ready. The CPU still runs serially, but the *waiting* now happens in parallel across connections.

On the same Wi-Fi network with the same protocol, this version handles **~250 QPS at 100% success**, with the knee starting around 300 QPS. About 3× more than the blocking version, and at much lower latency at any given load.

![Stress chart for the proper epoll server: staircase test from 10 to 500 QPS](../../assets/stress_chart_epoll_nonblocking.png)

#### Fixing errors I hit along the way

A couple of unrelated problems showed up when I cranked the stress test up:

- **mDNS resolution started failing under load.** Asyncio in my stress harness was opening thousands of connections per second, and each one was triggering an mDNS lookup. mDNS is multicast UDP with no retries; under burst load some lookups get dropped, and they bubble up as `EAI_NONAME` errors. **Fix:** resolve the Pi's IP once with `dscacheutil`, then pass the bare IP to the stress test. mDNS is for finding the Pi the first time; it's not built for benchmark traffic.

- **My Mac hit its file descriptor limit.** The default `ulimit -n` on macOS is something embarrassing like 256. Asyncio happily opens thousands of sockets at once and the kernel returns `EMFILE` (errno 24, "too many open files"). **Fix:** `ulimit -n 65536` in the shell before running the stress test.

Both of these are client-side problems, not server-side, but they show up as failures in the stress test, so they have to be eliminated before you can interpret the real server numbers.

#### Bottleneck is Wi-Fi

Once everything else is cleaned up, the server stops being the bottleneck and the Wi-Fi link itself becomes the limit. Two back-of-envelope calculations confirm this.

**Throughput math.** Each request lifecycle takes about 8 frames on the wire — SYN, SYN-ACK, ACK, the request data, the response data, FIN, FIN-ACK, final ACK. At 802.11 small-packet airtime of roughly 350 µs per frame (DIFS + backoff + PHY + payload + SIFS + ACK), the theoretical Wi-Fi PPS ceiling is about 2,800. Real-world degradation (interference, retransmits, neighbor traffic) drops sustained PPS to ~1,500–2,500. Divide by 8 frames per request: **190–310 QPS**. We measure ~250. Right in the middle.

**Latency math.** Each request needs 3 sequential round-trips — handshake, request/response, teardown. Ping RTT on this network is ~18 ms. 3 × 18 ms = **54 ms minimum**. Our measured p50 at 10 QPS is 53 ms. Within 2% of the theoretical floor.

So our throughput is Wi-Fi-PPS-bound, and our latency is Wi-Fi-RTT-bound. A faster server architecture cannot improve either number on this network. The bottleneck has migrated out of our code entirely.

## Latency optimisations

If the network is the limit, the only way to reduce latency is to change the network or change the protocol. In rough order of how much they help:

- **Use the bare IP, not the mDNS hostname.** Eliminates the variable cost of mDNS resolution per connection, which can otherwise add tens of milliseconds (or fail under load).
- Move closer to wifi access point.
- **Move to 5 GHz Wi-Fi if you're on 2.4 GHz.** Less interference from neighbors, microwaves, Bluetooth, and your fridge. Lower latency, much lower variance.
- **Move closer to the access point.** Stronger signal = fewer retransmits at the radio layer = lower jitter. This is the kind of thing you stop noticing until you measure it.
- **Switch to Ethernet.** This is the single biggest possible win. Gigabit Ethernet has sub-millisecond RTT and ~10,000× the small-packet PPS budget of typical Wi-Fi. The 50 ms p50 latency would drop to ~1 ms.
- **Persistent connections.** Right now we open a new TCP connection per request, paying ~3 RTTs of handshake/teardown overhead for every single response. If clients reused one connection for many requests (HTTP keep-alive style), we'd amortize that cost to near zero. This is a protocol change, not a network change.
- **`EPOLLET` instead of level-triggered.** Edge-triggered epoll fires only on state transitions instead of every time an fd is ready, so it generates fewer wakeups at high load. Worth a few percent at saturation; not a game-changer.

One thing that would *not* help on Wi-Fi: **adding more cores.** My Pi has 4 cores; the obvious next step is `SO_REUSEPORT` and one process per core, which on Ethernet would push throughput close to 4× higher. But on Wi-Fi the radio is shared by all processes on the machine — adding cores doesn't increase the PPS the radio can deliver. This is a classic "scale the wrong dimension" trap. The right answer here is to plug in an Ethernet cable first, *then* think about multiple processes.

## Conclusion

What I set out to build was an API my friend could hit from his laptop. What I actually built was a ~250-QPS odd-or-even oracle on a Pi in my closet, plus a stress-test harness, plus a non-trivial appreciation for how much of the modern internet's complexity exists because the simple version doesn't scale.

[TBA] Add stuff about Jatin Garg and some ideas to improve latency.

![](../../assets/cwp.jpeg)
Behold the Chaithanya's Web Platform in it's full glory.

[TBA Claude] Add a table for comparison of 4 different experiemnts, and their codes.

## Interesting questions I encountered

Here are some random questions that I asked myself that I found quite interesting:

- What is stopping a device from lying and saying that its "raspberrypi.local" during a mDNS protocol? How can this DNS resolution be verified?
- If know the laptop password of my friend then I can I just ssh into his machine if we are on the same wifi?
- My MacBook and my pi are in the same room. Why do they need a wifi access point to communicate with each other? Can we replicate this server logic on bluetooth?
- What is the difference between router and access point? My ISP gave me one box. Also will my server still work if I remove the ISP cable in my router?
- Why does someone outside my Wi-Fi network see only encrypted traffic, but someone inside my Wi-Fi can read my TCP server's plaintext bytes? Also why is SSH secure against both?
- If `recv()` and `send()` are POSIX-standardized, why isn't `epoll`? Why do BSD/macOS and Linux have completely different APIs (`kqueue` vs `epoll`) for the same fundamental job? This is important since I'm coding on two different kernels here. Linux kernel for my Pi and Unix for my MacBook.

## Next learnings

A few things I want to dig deeper into next:

- The full 7-layer OSI model — I now understand L2 (Ethernet, Wi-Fi MAC), L3 (IP), and L4 (TCP) reasonably well, but L5–L7 (TLS, session management, application protocols) are still hand-wavy in my head.
- How file descriptors actually work in the kernel — the `task_struct` → `files_struct` → `fdtable` chain, why `fork()` shares fds, how `select`/`poll`/`epoll` interact with the wait queues on each socket.
- The TLS handshake and how certificates actually chain back to a root of trust.
- WPA2/WPA3 internals — the 4-way handshake, the pairwise transient key, and why "having the Wi-Fi password" is enough to MITM your neighbors' traffic.
- BGP and how the public internet routes between autonomous systems.
- How CDNs and load balancers actually work under the hood — DNS-based vs Anycast, layer-4 vs layer-7, consistent hashing for cache locality.
- `SO_REUSEPORT` and multi-process servers (the natural next experiment, once I have an Ethernet cable).
- `io_uring`, the modern Linux replacement for `epoll` — completion-based instead of readiness-based, much less syscall overhead at very high QPS.

If you've made it this far, thanks for reading. The whole thing — server, stress harness, charts, scratch files — is on GitHub at https://github.com/Dragonado/IPServer.

## References

- https://mishal23.github.io/back-to-academia/
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — truly one of the best intro guides out there for learning about socket programming in Unix.
- [MIT 6.172, Lecture 10 — Measurement and Timing](https://youtu.be/LvX3g45ynu8?list=PLUl4u3cNGP63VIBQVWguXxZZi0566y7Wf) — taught me the philosophy and correctness of accurate measurement.
- [mDNS on Wikipedia](https://en.wikipedia.org/wiki/Multicast_DNS)
- [TCP Keepalive HOWTO](https://tldp.org/HOWTO/TCP-Keepalive-HOWTO/overview.html)
- [The method to epoll's madness](https://copyconstruct.medium.com/the-method-to-epolls-madness-d9d2d6378642) — Cindy Sridharan's classic write-up.
- [Slowloris attack on Wikipedia](https://en.wikipedia.org/wiki/Slowloris_(cyber_attack))
- Claude Code & Gemini for answering all my doubts, especially about the syscalls and the Pi.
