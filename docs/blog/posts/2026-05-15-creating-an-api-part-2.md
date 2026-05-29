---
date: 2026-05-15
---

# The simple task of hosting an API


## Motivation

A few weeks ago, a friend of mine asked me "@Chai, do you have an idle server running that I can ssh into? I want to host an experimental server that people can query for a course project." 

I was so mind-boggled with this question. Is this such a casual question to ask someone? Do students in this college just have idle servers running? Suppose I straight up gave access to my macbook to host his server, how does one even create a server from scratch and serve others?

I played it off cool and said "Nah bro, I dont have a server right now" to which he just said "Cool, I'll just rent an AWS box then. There should be a free trial available".

This was the end of the conversation but it stuck with me for quite a while. As a grad student at Georgia Tech in CS, I should not be stumped with this simple question, so I decided to learn a little about networks and write this blog on "**the simple task of hosting an API**".

Also, my friend is joining Jane Street. So maybe it is a casual question to ask for someone like him ¯\\_(ツ)_/¯


## Background

I used to work in Google as a SWE before and I always took infra work like hosting an API for granted. If I needed to serve a service to someone in Google, all I had to do was spin up a [Boq node](https://www.reddit.com/r/programming/comments/s21wti/comment/hsculqs/?utm_source=share&utm_medium=web3x&utm_name=web3xcss&utm_term=1&utm_content=share_button) (the internal microservice framework) and all the boilerplate work would be taken care of. I just needed to define the IO parameters, write the service part and send the endpoint (provided by the internal load balancer) to my colleague who wanted the service. 

Of course its only this simple for experimental work, for production services an engineer has to add unit tests, regression tests, end-to-end tests, functional tests, load tests, etc.

Joining Google Ads straight out of undergrad was an opportunity that I will forever be grateful for. I had immense growth both professionally and personally. I learnt a great deal of software engineering and how production code is supposed to be shipped that can handle planet-scale. So while I learnt a great deal of **using** planet-scale infra, I was severely lacking in technical abilities of actually **building** a planet-scale infra. [TBA Cluade add something of this flavour] In return for learning a great deal about software engineering practices, writing docs, domain knowledge and intuition for Ads, insight about user/advertiser behaviour, I didnt get to work on techincal stuff like improving the load balancer or decreasing the boot time latency of [Borg](https://en.wikipedia.org/wiki/Borg_(cluster_manager)) machines.

So I joined back academia. Case in point from another Google engineer (and friend) https://mishal23.github.io/back-to-academia/. We share the same sentiment but reached different conclusions. He chose to stay in Google and pursued an online masters from Georgia Tech part time. I chose to leave Google and pursued an in-person masters degree from Georgia Tech full time. Note to the reader: There is no right/wrong answer in our decisions here.

## Hardware

The easiest solution was to input my credit card into one of the cloud providers (AWS/GCP/Oracle) and get a instance of one of their machines that I can ssh into. But I don't want to do that for several reasons.

So what I'm gonna do is use my fully decked out Raspberry Pi 5 that has been sitting idle in my closet for the past 3 years. This Pi comes with a cooling fan + 128 GB SD storage + 2.4GHz quad-core Arm Cortex-A76 + 8GB RAM running the Linux based Raspberry Pi OS.

Looks something like this before assembling:

![](https://www.canakit.com/Media/700/2915.jpg)

This is the machine that will be hosting my server. I will be using my Macbook M1 Air that acts as a client querying the server.

## Setup

### Big picture

![Network setup: MacBook ↔ Wi-Fi access point ↔ Raspberry Pi, with SSH (encrypted) and the actual client/server traffic (unencrypted) as the two paths](../../assets/api-setup-network-diagram.jpg)

As mentioned before, I will be using my Pi as the server and I will be editing the Pi files on my Mac via SSH. I will also be using my mac to act as a client to ping the server. So there are two paths from my Mac to the pi.

One thing to note is that my ISP doesn't provide a global static IP address. So for now I can only access this server from the devices connected to my wifi. I also don't want to disble my firewalls and expose any port to the open internet. I am smart enough to know that I'm not yet skilled in cybersec to play with these configurations.

Before running the server, we need to get the machine running first. The Canakit version I bought comes with the Raspberry Pi OS downloaded so I really don't have to do anything here. All I need to do is turn it on and enable ssh on this machine. 

```bash
chaithu@raspberrypi:~ $ sudo systemctl enable --now ssh
```

And now I ssh into the machine from my mac.

```zsh
chaithanyashyamd@Chaithanyas-MacBook-Air ~ % ssh chaithu@raspberrypi.local
```

Wait, I can just do `chaithu@raspberrypi.local` instead of inputting an IP address? 

### mDNS

IP addresses provided by my WiFi access points are not really static. They change every X minutes (TBA Claude). Even though its a slow refresh, it's a bit of a pain to find out its IP address each time so I just use the **mDNS**, I'm sure nothing can go wrong with this simple hack. (_subtle foreshadowing_)

mDNS is a really neat protocol for resolving hostnames to IP addresses of devices in a local network. Basically my Mac sends a multicast query to all the devices in my network. My Pi sees this call and responds with a similar multicast query with the IP address it owns. 

```zsh
chaithanyashyamd@Chaithanyas-MacBook-Air ~ % ping -q -c 10 raspberrypi.local 
PING raspberrypi.local (192.168.1.24): 56 data bytes

--- raspberrypi.local ping statistics ---
10 packets transmitted, 10 packets received, 0.0% packet loss
round-trip min/avg/max/stddev = 16.264/30.368/52.540/12.168 ms
```

We can see that the `raspberrypi.local` resolves to `192.168.1.24` and a ping takes ~30ms on average.

### Server Code

Now this is the most fun part! Writing the code for the server. For maximum learning I decided to use C/C++. I would use pure C but multithreading in C is an absolute pain point and I would rather much deal with `std::thread` for this project.

Lets start with a very simple bare-bones server. I wont explain most of the code since it's basic stuff. I highly recommend reading the Beej networking guide to learn the basics. It's a really good tutorial after which I went from 0 to basic socket programmer that can understand the man pages of the network syscalls.

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

- Server is running on port 8080
- Server accepts atmost 1 connection in its queue as seen on the `listen()` command
- All the API is doing is returning if the given number is Odd or even with some basic error handling.

Let's try to run it on my Pi.

```bash
chaithu@raspberrypi:~/Desktop/server $ g++ single_thread.cpp -o server && ./server
```

and on my Mac I query the server.

```zsh
chaithanyashyamd@Chaithanyas-MacBook-Air ~ % echo "100" | nc raspberrypi.local 8080 
Even
```

Absolute success! Most of it is basic boiler plate code. If you want to change the API, literally the only thing that needs to be changed is the `perform_logic_and_populate_response` function.

Also a neat thing to notice is that this communication is completely unencrypted (you might have observed this in my diagram). So anyone inside my wifi network can snoop around and intercept this communication with a simple `tcpdump` command.

## Stress test

Sure, technically this is a valid server API that people can now query for their needs. But how good is it? How good can we make it? What does "good" even mean?

There are many measures of what a good server should be. For example you can optimise for metrics like:

- Throughput - how many queries can the server respond to?
- Latency - how long does it take to get a response?
- Reliability - Does the server have any downtime?
- CPU usage
- Memory usage
- etc

For now, I can only care about throughput and latency. This setup is pretty bad reliability wise because it's literally just one machine that could go off at any time. CPU and memory is anyway a physical limitation that I can't really change.

### Baseline

I asked Claude to make a stress test script using python. So with this I can basically control the QPS (queries per second) and then measure the success rate and the latencies.

You can find it on my github [here](TBA Claude).

The setup is, I send a linearly increasing load for the first few seconds to eliminate any cold start issues occuring on the client or network or the server side. I then maintain a steady state of given QPS limit for the given duration.

Latency numbers are only taken from the steady state phase but I still chart the entire load.

Here is a basic sanity check of the script:

```zsh
(.venv) chaithanyashyamd@Chaithanyas-MacBook-Air IPServer % python3 stress.py --host raspberrypi.local --qps 10 --warmup 1 --steady 5

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

Yay, looks like everything works correctly. Average latency is 300ms whereas 99% of the packets complete within 1.1s

This is a good baseline.

NOTE: These latency numbers bascially measure the network and socket handling. The actual API itself (code inside the while loop) is super fast. All it does is, recieve 20 bytes of data, does a few operations and branches (deciding if number is odd/even) and then sends 20 bytes of data. All this is in the order of micro-seconds and hence not a bottleneck for our case. 

[TBA Claude: add the chart]

### Maximum throughput

#### Increasing the Accept Queue size because someone attacked my server

Lets just run the sanity check again.

```zsh
(.venv) chaithanyashyamd@Chaithanyas-MacBook-Air IPServer % python3 stress.py --host raspberrypi.local --qps 10 --warmup 1 --steady 1 

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

Whaaaaat? Why is everything failing? My Pi is working completely fine and there was no downtime. The server is still running, I literally queried it a bunch of times just before running the above stress test. I haven't rebuilt the binary or anything. And I'm just running a small stress test (10QPS) so its not a performance issue. Yet, the queries are timing out.

Can you guess why? Its a not-so popular cybersecurity attack that accidentally happened here due to my bad coding configurations. No, its not DDoS but close.

[TBA Claude] add spoiler html tag here.

It's the [Slowloris attack](https://en.wikipedia.org/wiki/Slowloris_(cyber_attack)), more technically it's a Slowloris-like attack since this attack works on HTTP requests whereas my issue is a TCP one. It's basically a type of [slow DoS attack](https://en.wikipedia.org/wiki/Slow_DoS_attack) where a malicious user causes unavaibility of the service by sending partial requests to keep the connection alive and reducing the server's connection capacity. Obviously mine wasn't a malicious user but an accidental occurence of this attack.

My current implementation of the server tells the kernel to only keep an accept queue of size `1` as seen on my `listen(fd, 1)` command. So any connections arriving that come after this queue are dropped. This is whats happening here, my server has a full queue and is unable to accept any more requests.

Why is my queue full though? Shouldn't my server just `accept()` the connection and serve it and free the queue?

It's because my beautiful single-threaded program is stuck on the blocking `recv()` call and cannot move on. I mentioned that I ran a bunch of queries before running this one and I didn't rebuild the binary right? Turns out that the one of the queries did not behave correctly.

It looks like one of the queries, after establishing a connection with my Pi, never sent its request bytes via `send()` (due to WiFi packet loss maybe? it happens a lot so not surprised) and so my Pi is keeping the connection alive in hopes of getting a request back. This is the slowloris-like attack that happened to me :'(. 

Unfortunately, [TCP keep-alive](https://tldp.org/HOWTO/TCP-Keepalive-HOWTO/overview.html) config is set off by default, so the server would be held hostage forever by one bad packet loss. While turning on the TCP keep-alive would eventually clean up connections lost to packet loss, a malicious actor could still bypass by ack'ing the TCP keep-alive calls but still not send any request bytes.

I would have a very hard time to debug this flaky issue without Claude. It gave me a bunch of things I could try and I was able to diagnose this and confirm that this is indeed the case using the `ss` (socket statistics) command to inspect the socket connections on the `8080` port.

The fix? Make `recv` non blocking using the `epoll` syscall. Using `epoll` we can choose to only repy to those connections that are active.

Workaround for now:

- Kill the server
- Increase the queue size to maximum (which is `4096` for my Pi)
- Set a timeout on the `recv()` syscall (I will use this stopgap measure for now instead of `epoll`)
- Rebuild the binary.
- Start the server again.

Increasing the accept queue size increases the the throughput of the server because we can answer more queries by queuing them instead of dropping. However, this clearly doesn't change the number of queries we can respond to **per second** because we are not any faster.

This gives us around 80QPS at the same 1.1s latency

![](../../assets/stress_chart_single_thread_80.png)

To make our server answer more queries per second, we must add a bit of non-blocking because we are making our thread sit idle on the`recv` to get its bytes from a particular connection but there might be another connection with its request bytes ready that can be served. 

### Epoll

#### Epoll + blocking I/O

Gives me around 100 QPS but its because I wrote it wrong.

#### Epoll + non blocking I/O


#### Fixing errors

mDNS cannot handle such large query requests. so we need to manually resolve.
Increase stack limit in server with ulimit.



#### Bottleneck is wifi

## Latency optisations

mDNS bypass
2.4G -> 5G
move laptop closer to wifi
ethernet cable
Even though my pi has 4 cores, multi-threading wont help in this case because its limited by wifi.
epoll LT -> ET because fewer wakeups

## Conclusion

I need to learn more about networks. Getting a good grasp on all the 7 layers of OSI is necessary. I need to know more about file descriptors, how epoll works, SYN queues, TLS handshakes, WPA security, [TBA claude].

## References

- https://mishal23.github.io/back-to-academia/.
- [Beej](https://beej.us/guide/bgnet/) - Truly one of the best intro guides out there for learning about socket programming in Unix.
- [10. Measurement and Timing](https://youtu.be/LvX3g45ynu8?list=PLUl4u3cNGP63VIBQVWguXxZZi0566y7Wf) - taught me the philosophy & correctness of accurate measurement.
- [mDNS wikipedia](https://en.wikipedia.org/wiki/Multicast_DNS)
- Claude Code & Gemini for answering all my doubts, especially about the syscalls and the Pi.
- https://tldp.org/HOWTO/TCP-Keepalive-HOWTO/overview.html
- Epoll: https://copyconstruct.medium.com/the-method-to-epolls-madness-d9d2d6378642