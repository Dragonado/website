---
date: 2026-08-02
---

# Remoting Metal: turning Apple’s local GPU API into a network protocol

## Motivation

I've said this before and I'll say it again. Compared to my experience in corporate, Grad school is a very interesting place to be in. You meet people from many walks of life and people who will transition to all other walks of life. You get a glimpse into an area that you would never have had the opportunity to know about as compared to a static workplace. 

<!-- more -->

Anyways, One of my friends introduced me to a very interesting startup from Georgia Tech (#GoJackets! :bee:) called [ThunderCompute](https://www.thundercompute.com). They are riding the AI wave and are selling cheap GPU compute. Possibly the cheapest I have ever found so far (?!). The closest I have seen are spot instances given by AWS/GCP/Azure but those are quite risky to run ML workloads since they can be terminated at the providers will. 

I was very curious as to how they could possibly offer such low GPU prices with these features:

1. on-demand
2. sole-tenancy 
3. No forced preemption

Surely these must be VC subsidised GPU and hence bleeding money right? But no, I don't think so. 

The simple answer is: Oversubscription :stars:

## Oversubscription

What does what mean? Basically, you sell an item to a user. Then, when the first user is not using the item, you sell the same item to a different user. With this secret technique, you have essentially reduced costs while also increasing revenue.

Oversubscription is actually a pretty common thing. It is literally how banks work, they lend out more money than they actually have. But in a more relevant example, cloud providers also do this.

For example, if you pay for 100GB disk space on your serverless function and you use only 20GB of it, your allocated hardware is maybe oversubscribed to other users. Cloud provider makes a huge saving (which in turn reduces prices) but you still own the right to use your entire 100GB disk space. You gotta be smart enough to keep your utilization % high.

What if happens if every single user decides to utilize 100% of their resources? Idk man what happens if everyone withdrew all their money from the bank? Civilization collapses or sumthing. Just have faith in the [LLN gods](https://en.wikipedia.org/wiki/Law_of_large_numbers) (not a typo) and pray this doesn't happen.

I was very interested how ThuNdeRcompute (TNR) manages to oversubscribe their GPU. The way they do it so simple yet so smart. For most ML workfloads, the GPU is idle a lot of the time. What if you could run that GPU on a different ML workload that someone else is waiting on? That would be nice but we can't do that since the GPU is literally attached to the CPU that the first user is doing. Oh, but then what if we detach the GPU and make it remote? That way could pool GPU jobs from different users and schedule them as we want. Perhaps connect the CPU and GPU via a internet protocol? Yeah let's do this and call it GPU-over-TCP :absolute-cinema:

Yeah so basically, they intercept your code's GPU CUDA calls, forward them over the internet via TCP to a real GPU, compute it there, give back the result via TCP again. Sounds simple but insanely hard to achieve. But once you are able to do it, you can oversubscribe your GPUs and make compute cheap and everyone wins!

The obvious downsides are:

-  _Network latency & throughput_: Moving data from CPU to GPU via PCIe bus is faster and has more throughput than TCP.
-  Not suitable for _all_ workloads: TNR makes a profit by identifying GPU idleness and exploiting it. However, if your workload has the GPU running all the time (for example calculating hashes for a certain reason :hint-hint:) then it's not a particularly useful load for the company.

Fascinated by all this, I decided to make my own GPU-over-TCP but since I'm constrained to my dusty 6-year old Macbook air with M1 chip with a single GPU, I have to make several changes to what TNR does.

Btw they also have a student program where you get a free $20 in GPU credits (~9hrs of a H100).

## Hardware & Setup

My hardware: 

![The M1 MacBook Air used to build and test the Metal API Remoter](../../assets/metal-api-remoter-m1-macbook-air.jpg)

You might be wondering where the other hardware is located? After all I have to pass data from one device to the other over the network.

Yeah, I cant be bothered to rent out at a cloud apple GPU instance and go through the hassle of setting up the networking for it. I'm just gonna forward the network calls to localhost and have my own device intercept and run its own code lol.

It'll make more sense when you read the next sections but here is the architecture setup:

![Metal API Remoter architecture: client code uses a compile-time shim that sends calls over the network to a server with the GPU](../../assets/metal-api-remoter-architecture.jpg)


### Ominous Premonition

This seems like a nice idea. Just copy what TNR does but do it for metal-cpp. No one else in the world seems to have done anything like this. Exciting!

However there are atleast two reasons why no one has done this and why my project is a much much harder to solve than TNR:

1. Lack of ABI interception of the metal framework.
2. Apple's Unified Memory architecture.

<!-- ### The basic client/server goal -->

<!-- lorem ipsum -->

<!-- ### What this project does—and does not—claim -->

<!-- lorem ipsum -->

<!-- ## 2. Why Metal is harder to intercept than CUDA -->

<!-- ### CUDA's C ABI and dynamic-linker boundary -->

<!-- lorem ipsum -->

<!-- ### Metal's Objective-C message dispatch -->

<!-- lorem ipsum -->

<!-- ### Why precompiled Metal binaries are outside the MVP -->

<!-- lorem ipsum -->

<!-- ## 3. The compile-time `metal-cpp` header shim -->

<!-- ### Turning `MTL::` into `MetalShim::` -->

<!-- lorem ipsum -->

<!-- ### What the compiler emits after substitution -->

<!-- lorem ipsum -->

<!-- ### Source compatibility versus binary transparency -->

<!-- lorem ipsum -->

<!-- ## 4. Turning Metal's object graph into remote handles -->

<!-- ### Devices, queues, libraries, functions, and pipelines -->

<!-- lorem ipsum -->

<!-- ### Why object creation is synchronous RPC -->

<!-- lorem ipsum -->

<!-- ### `NS::String*` is data, not a remote GPU object -->

<!-- lorem ipsum -->

<!-- ## 5. Create, record, commit, and wait -->

<!-- ### Creation calls cross the network -->

<!-- lorem ipsum -->

<!-- ### Recording calls stay local -->

<!-- lorem ipsum -->

<!-- ### `commit()` is the serialization boundary -->

<!-- lorem ipsum -->

<!-- ### `waitUntilCompleted()` is the completion boundary -->

<!-- lorem ipsum -->

<!-- ## 6. The pointer problem: shadow buffers and coherence -->

<!-- ### Why `Buffer::contents()` cannot send a pointer over the network -->

<!-- lorem ipsum -->

<!-- ### Client-side shadow allocations -->

<!-- lorem ipsum -->

<!-- ### Copying inputs at commit -->

<!-- lorem ipsum -->

<!-- ### Copying outputs after completion -->

<!-- lorem ipsum -->

<!-- ### The unsupported mid-flight CPU/GPU access pattern -->

<!-- lorem ipsum -->

<!-- ## 7. The protobuf protocol -->

<!-- ### Handles and create/release RPCs -->

<!-- lorem ipsum -->

<!-- ### Encoding a command buffer as metadata and bytes -->

<!-- lorem ipsum -->

<!-- ### How protobuf frames repeated fields and `bytes` -->

<!-- lorem ipsum -->

<!-- ## 8. gRPC is parallel: protecting shared server state -->

<!-- ### The 100-client race-condition experiment -->

<!-- lorem ipsum -->

<!-- ### What the mutex actually protects -->

<!-- lorem ipsum -->

<!-- ### Why one global mutex is acceptable for the MVP -->

<!-- lorem ipsum -->

<!-- ### Why the mutex must not cover GPU waits -->

<!-- lorem ipsum -->

<!-- ## 9. The asynchronous command-buffer scheduler -->

<!-- ### RPC handlers enqueue jobs -->

<!-- lorem ipsum -->

<!-- ### One scheduler thread submits to Metal -->

<!-- lorem ipsum -->

<!-- ### FIFO ordering and same-queue dependencies -->

<!-- lorem ipsum -->

<!-- ### Admission order is not GPU preemption -->

<!-- lorem ipsum -->

<!-- ## 10. Resource lifetime across the network boundary -->

<!-- ### Why native Metal retains resources after commit -->

<!-- lorem ipsum -->

<!-- ### The current release-before-wait limitation -->

<!-- lorem ipsum -->

<!-- ### What `PendingJob` must eventually own -->

<!-- lorem ipsum -->

<!-- ## 11. What the working demo proves -->

<!-- ### A vector-add program with the same client source -->

<!-- lorem ipsum -->

<!-- ### Remote execution and result verification -->

<!-- lorem ipsum -->

<!-- ### Concurrent clients sharing one server and GPU -->

<!-- lorem ipsum -->

<!-- ### What has not been measured -->

<!-- lorem ipsum -->

<!-- ## 12. Thunder Compute and TNR: two different tradeoffs -->

<!-- ### Thunder's exclusive GPU-lease model -->

<!-- lorem ipsum -->

<!-- ### This project's command-buffer multiplexing model -->

<!-- lorem ipsum -->

<!-- ### VRAM, fairness, isolation, and predictability -->

<!-- lorem ipsum -->

<!-- ### Why neither design dominates the other -->

<!-- lorem ipsum -->

<!-- ## 13. The semantic contract -->

<!-- ### Preserving Metal meaning, not Metal timing -->

<!-- lorem ipsum -->

<!-- ### The supported-shim boundary -->

<!-- lorem ipsum -->

<!-- ### Honest claims about valid programs and dishonest clients -->

<!-- lorem ipsum -->

<!-- ## 14. What remains -->

<!-- ### Native-compatible resource lifetime -->

<!-- lorem ipsum -->

<!-- ### Broader Metal storage and synchronization modes -->

<!-- lorem ipsum -->

<!-- ### Session isolation and security -->

<!-- lorem ipsum -->

<!-- ### Scheduler instrumentation and utilization experiments -->

<!-- lorem ipsum -->

<!-- ## 15. Closing perspective -->

<!-- ### A working remoter before a production virtual GPU -->

<!-- lorem ipsum -->

<!-- ### The next experiment -->

<!-- lorem ipsum -->
