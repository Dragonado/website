---
date: 2026-05-15
---

# The simple task of hosting an API - Part 2 [TBA]


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

Absolute success!

Most of it is basic boiler plate code. If you want to change the API, literally the only thing that needs to be changed is the `perform_logic_and_populate_response` function. Wow that's very generic and solid. Maybe I'm now ready to join the Google Boq team?


## References

- [Beej](https://beej.us/guide/bgnet/) - Truly one of the best intro guides out there for learning about socket programming in Unix.
- [10. Measurement and Timing](https://youtu.be/LvX3g45ynu8?list=PLUl4u3cNGP63VIBQVWguXxZZi0566y7Wf) - taught me the philosophy & correctness of accurate measurement.
- [mDNS wikipedia](https://en.wikipedia.org/wiki/Multicast_DNS)
- Claude Code & Gemini for answering all my doubts, especially about the syscalls and the Pi.