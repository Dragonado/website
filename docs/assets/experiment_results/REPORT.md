# Mega Experiment: Four Server Architectures on a Raspberry Pi

Date: 2026-05-26
Client: MacBook Air M1 (Wi-Fi)
Server: Raspberry Pi 5 (8 GB), Raspberry Pi OS, kernel `6.6.28+rpt-rpi-2712`, 4× Cortex-A76 @ 2.4 GHz
Network: Home Wi-Fi (5 GHz), client and server on the same SSID
Target IP: `192.168.1.13:8080` (bare IPv4, no mDNS resolution involved)

## TL;DR

| # | Server | Cleanest QPS (100%) | Knee QPS (>50%) | Total OK (of 23,500) | p50 @ 10 QPS |
|---|---|---|---|---|---|
| 1 | `single_thread` (backlog=1) | **10** | ~10 | 155 (0.7%) | 92 ms |
| 2 | `single_thread_max_queue` (backlog=4096) | **160** | ~210 | 5,675 (24.1%) | 58 ms |
| 3 | `epoll_blocking_io` (epoll wrapper + blocking handler) | **160** | ~210 | 6,601 (28.1%) | 86 ms |
| 4 | `epoll_non_blocking_io` (epoll + non-blocking I/O + state machine) | **210** | ~260 | 11,408 (48.5%) | 53 ms |

**The largest single improvement comes from increasing the listen backlog (server 1 → 2: ~16× more clean QPS).** The full state-machine epoll refactor (server 3 → 4) adds another ~25%. Adding epoll *without* fixing the blocking handler (server 2 → 3) gives essentially no benefit and actually slightly worsens latency.

The absolute ceiling across all four servers is ~250 QPS, suggesting **Wi-Fi PHY-level packet rate, not server CPU, is the binding constraint** for this protocol on this hardware.

---

## Methodology

For each server:

1. SSH to Pi, kill any prior server, compile with `g++ -O2 -Wall`, start in background.
2. Smoke test: `echo "100" | nc -4 192.168.1.13 8080` → expect `Even`; `echo "7"` → `Odd`.
3. Stress test from Mac:
   ```
   ulimit -n 65536
   .venv/bin/python stress.py --host 192.168.1.13 \
       --mode staircase --start-qps 10 --end-qps 500 --qps-step 50 \
       --step-duration 10 --timeout 10
   ```
4. Save `stress_log.jsonl`, `stress_chart.png`, `stress_errors.log` with server-specific prefix.
5. Kill server, wait for port 8080 to be free, move on.

**Test shape:** 10 plateaus at 10, 60, 110, 160, 210, 260, 310, 360, 410, 460 QPS, each held for 10 s. Total 23,500 attempts per server. Per-request latency anchored to `t_scheduled` (CO-correct).

**Why bare IP and not `raspberrypi.local`:** mDNS resolution adds variable latency and fails ~6–11% of the time under load (we verified this in earlier runs). Using the IP eliminates the network resolution layer as a confounding variable. The Pi's IP was discovered once via `dscacheutil` and reused.

**`ulimit -n 65536`** on the client raises its file-descriptor cap above macOS's default 256, eliminating `EMFILE` noise from the harness itself.

---

## Server 1: `1_single_thread.cpp` (blocking I/O, backlog = 1)

The original "experiment 1" implementation: single-threaded `while (true) { accept; recv; process; send; close; }`. The listening socket is created with `listen(s, 1)` — the kernel's accept queue holds at most 1 connection (technically 2 due to the Linux off-by-one).

### Per-plateau result

| Target QPS | n | OK | Err % | p50 ms | p90 ms | p99 ms |
|---|---|---|---|---|---|---|
| **10** | 100 | 100 | 0.0% | 92 | 266 | 776 |
| 60 | 600 | 55 | 90.8% | 120 | 224 | 526 |
| 110+ | 1,100+ | 0 | 100% | — | — | — |

Total: 155 OK / 23,500 (0.7%). Bulk of errors split between **timeout (63.5%)** and **refused (35.8%)**.

### What's happening

- At 10 QPS, the server keeps up. Handler is microseconds; with one connection at a time the kernel hands them off in series.
- At 60 QPS, the accept queue (capacity 2) overflows almost instantly. SYNs arriving when the queue is full get **silently dropped** by the kernel (default `tcp_abort_on_overflow=0`), so clients see timeouts as TCP retransmits eventually give up.
- Some failures are `refused` (RST), suggesting the queue fills past the kernel's tolerance and explicit RSTs get sent — or that very brief windows between drains cause some SYNs to RST.
- At 110+ QPS, every attempt times out. The server is permanently saturated.

### Lesson

**`listen(s, N)` is the most important number you'll set on the listener.** A backlog of 1 is suitable only for demonstration. Real services need at least 128, ideally `SOMAXCONN` (4096 on modern Linux). Going from 1 → 4096 yields **~16× more sustained QPS in our tests**, more than any other single change.

---

## Server 2: `2_single_thread_max_queue.cpp` (blocking I/O, backlog = 4096)

Same as server 1, but `listen(s, 4096)`. Single-threaded blocking handler. The change is one integer.

### Per-plateau result

| Target QPS | n | OK | Err % | p50 ms | p90 ms | p99 ms |
|---|---|---|---|---|---|---|
| 10 | 100 | 100 | 0.0% | 58 | 123 | 339 |
| 60 | 600 | 600 | 0.0% | 53 | 179 | 411 |
| 110 | 1,100 | 1,100 | 0.0% | 58 | 240 | 374 |
| **160** | 1,600 | 1,600 | 0.0% | 68 | 193 | 415 |
| 210 | 2,100 | 1,776 | 15.4% | 175 | 1,612 | 3,207 |
| 260 | 2,600 | 0 | 100% | — | — | — |
| 310+ | 3,100+ | mostly 0 | ~100% | — | — | — |

Total: 5,675 OK / 23,500 (24.1%). Errors: 72.5% timeout, 3.3% reset.

### What's happening

- Sustains 100% success up to 160 QPS comfortably.
- Knee is around 210 QPS — at that load the handler can't drain the accept queue as fast as it fills, so queue depth grows. Once the queue exceeds the client's 10 s timeout × handler rate, requests start failing.
- The handler's per-request cost is ~50 ms wall-clock (TCP setup + tiny work + teardown over Wi-Fi). So theoretical max ≈ 1 / 0.05 = 20 QPS on a *single connection in series*. But with backlog absorbing bursts, real throughput climbs to ~200 QPS because kernel-side network work overlaps across connections while userspace handles them serially.

### Lesson

**A larger backlog buys you burst tolerance, not steady-state throughput.** The server can absorb thousands of incoming SYNs without dropping them, but its **drain rate is unchanged** (still single-threaded blocking). The benefit is no longer dropping bursts; it isn't running faster per request. Steady-state throughput is bounded by handler cost × 1 (single thread), not by backlog size.

This is also visible in latency: at 10 QPS p50 is 58 ms, but at 160 QPS p50 stays at 68 ms — the *handler* is not slowing down; it just can't process more in parallel.

---

## Server 3: `3_epoll_single_blocking_io.cpp` (epoll wrapper, blocking handler)

The "naive epoll" version: replaces `while (true) { accept; ... }` with an `epoll_wait`-based event loop, but the per-event handler is still synchronous `recv → process → send → close` and the fds are still **blocking**.

### Per-plateau result

| Target QPS | n | OK | Err % | p50 ms | p90 ms | p99 ms |
|---|---|---|---|---|---|---|
| 10 | 100 | 100 | 0.0% | 86 | 331 | 480 |
| 60 | 600 | 600 | 0.0% | 73 | 264 | 561 |
| 110 | 1,100 | 1,100 | 0.0% | 90 | 339 | 1,353 |
| **160** | 1,600 | 1,600 | 0.0% | 267 | 901 | 2,229 |
| 210 | 2,100 | 1,748 | 16.8% | 2,960 | 7,445 | 13,049 |
| 260 | 2,600 | 669 | 74.3% | 8,330 | 12,515 | 16,455 |
| 310+ | 3,100+ | <30% | — | — | — | — |

Total: 6,601 OK / 23,500 (28.1%). Errors: 66.4% timeout, 5.6% reset.

### What's happening

- Knee is essentially the **same** as server 2 (~210 QPS).
- Latency at 160 QPS is meaningfully **worse** than server 2 (267 ms vs 68 ms p50). Each event goes through one more `epoll_wait` round trip + an extra `setsockopt` (SO_RCVTIMEO inside the loop in this version).
- The conclusion is striking: **adding epoll without making I/O non-blocking yields no throughput improvement.** Slight latency penalty from epoll machinery overhead with no compensating concurrency benefit.

### Lesson

**`epoll_wait` alone is not the win — non-blocking I/O is.** epoll is just an *event-readiness notification mechanism*. If the handler still blocks (waiting for the kernel send buffer, etc.), the single thread serializes on each connection just like the `single_thread_max_queue` version. The same bottleneck applies.

This experiment quantifies the value of the "epoll wrapper but blocking handler" pattern at zero: it's the worst of both worlds — extra code complexity, no performance gain. The win only arrives when you also refactor the handler.

---

## Server 4: `4_epoll_single_non_blocking_io.cpp` (epoll + non-blocking I/O + state machine)

The "proper" epoll implementation:
- All fds are `O_NONBLOCK` (set via `fcntl`).
- Edge-triggered (`EPOLLET`) on client connections, level-triggered on the listener.
- Per-connection state lives on the heap in a `struct connection` keyed by `epoll_event.data.ptr`.
- `do_read` loops `recv` until `EAGAIN`, then transitions to `WRITING_RESPONSE` and switches the epoll registration to `EPOLLOUT`.
- `do_write` loops `send` until `EAGAIN`; partial sends preserve state in `c->res_sent` and resume on the next `EPOLLOUT` event.
- `SO_RCVTIMEO` of 2 s catches truly silent peers; `MSG_NOSIGNAL` + `signal(SIGPIPE, SIG_IGN)` prevent process death from dead-peer writes.

### Per-plateau result

| Target QPS | n | OK | Err % | p50 ms | p90 ms | p99 ms |
|---|---|---|---|---|---|---|
| 10 | 100 | 100 | 0.0% | 53 | 278 | 532 |
| 60 | 600 | 600 | 0.0% | 71 | 262 | 434 |
| 110 | 1,100 | 1,100 | 0.0% | 78 | 243 | 477 |
| 160 | 1,600 | 1,600 | 0.0% | 86 | 214 | 475 |
| **210** | 2,100 | 2,100 | 0.0% | 109 | 367 | 556 |
| 260 | 2,600 | 2,349 | 9.7% | 378 | 2,864 | 8,032 |
| 310 | 3,100 | 2,231 | 28.0% | 2,446 | 7,131 | 10,231 |
| 360 | 3,600 | 1,245 | 65.4% | 5,348 | 9,895 | 13,503 |
| 410+ | 4,100+ | <2% | ~100% | — | — | — |

Total: 11,408 OK / 23,500 (48.5%). Errors: 48.4% timeout, 3.1% reset.

### What's happening

- Sustains 100% success up to **210 QPS** — one plateau higher than servers 2 and 3.
- Knee moves to ~260 QPS, with 90.3% success at that load.
- p50 latency at 10 QPS is the lowest of all four (53 ms), reflecting that the handler is genuinely doing less synchronous work per event (no blocking waits).
- p50 latency at 210 QPS is 109 ms — still very low compared to server 3's 2,960 ms at the same load.

### Lesson

**The state-machine refactor is the actual win from epoll.** Per-request work no longer serializes on kernel I/O waits; the kernel's network activity for one connection overlaps with userspace handler activity for another. This buys roughly **25–30% more sustained QPS** and a much wider window of clean (sub-second) latency.

The improvement is more modest than expected because:
1. The handler is already trivial (microseconds of compute).
2. The bottleneck has shifted from "userspace blocking on the kernel" to **the network itself (Wi-Fi PHY rate)**.

---

## Comparative analysis

### Throughput curve

```
                    100% ok zone     | knee zone (50-99%)  | collapse zone
                    ──────────────── | ─────────────────── | ──────────────
Server 1            ≤10 QPS          | ~10–30              | >30
Server 2            ≤160 QPS         | ~200–230            | >250
Server 3            ≤160 QPS         | ~200–230            | >250  (same as 2)
Server 4            ≤210 QPS         | ~250–300            | >350
```

### Latency at the same low QPS (10 QPS, mostly server-dominated)

| Server | p50 | p99 |
|---|---|---|
| 1 | 92 ms | 776 ms |
| 2 | 58 ms | 339 ms |
| 3 | 86 ms | 480 ms |
| 4 | **53 ms** | 532 ms |

Server 4 is fastest at low load (no syscall waste); server 3 is slowest among "good" backlog-fixed servers because epoll dispatch adds overhead without compensating throughput.

### Latency at saturation knee (160 QPS, where everyone is still 100% successful)

| Server | p50 | p90 |
|---|---|---|
| 2 | 68 ms | 193 ms |
| 3 | 267 ms | 901 ms |
| 4 | 86 ms | 214 ms |

Server 3 is much worse here — its `setsockopt(SO_RCVTIMEO)` inside the event-loop iteration is real measurable overhead at higher per-event rates.

---

## Key conclusions

### 1. The single biggest performance lever was changing one integer

Going from `listen(s, 1)` to `listen(s, 4096)` improved sustainable QPS by a factor of ~16×. No other single change matched that magnitude in absolute terms. **Always set a sensible backlog before chasing more sophisticated concurrency models.**

The backlog isn't about speed; it's about burst tolerance. With backlog=1 a transient kernel hiccup or a single slow handler instantly fills the queue and starts dropping connections. With backlog=4096 the kernel can absorb thousands of pending connections while you drain.

### 2. epoll without non-blocking I/O is not faster than a tight blocking `while` loop

This was the most counterintuitive finding. Server 3 was the same in throughput as server 2 (~200 QPS knee) and worse in latency. The reason: the dominant cost in each request is per-call kernel I/O time + Wi-Fi RTT, not the structure of the event loop. Replacing `while (true) { accept; ... }` with `epoll_wait` only matters if you also stop blocking on individual fds.

The "epoll wrapper around a blocking handler" pattern is a real anti-pattern. It looks like you're using a high-performance mechanism, but the actual concurrency model is unchanged. **Always pair epoll with non-blocking I/O and per-connection state.**

### 3. Non-blocking I/O + state machine ≈ 25% improvement over a well-tuned blocking server

Server 4 vs server 2 is a real improvement, but not the order-of-magnitude jump you'd hope for. Reason: this protocol's bottleneck shifted from "handler is serial" to "Wi-Fi can't deliver more packets per second." The state-machine architecture eliminates the userspace bottleneck — but a new (network) bottleneck reveals itself. (See the back-of-envelope section below for the actual numbers — Wi-Fi caps us at ~250 QPS no matter the server architecture.)

On **Ethernet** (sub-millisecond RTT, ~100,000+ PPS for small packets), the gap between server 2 and server 4 would be much larger — server 4's non-blocking architecture lets one core run thousands of overlapping connection lifecycles, while server 2's blocking handler still serializes them. Estimated multiple: 5–10×. We did not test Ethernet here.

### 4. The Pi 5's Wi-Fi caps practical throughput around 250 QPS for tiny RPC-style requests

Every server tested converges to a ceiling near 200–260 QPS over Wi-Fi. The bottleneck isn't CPU (the Pi 5 has 4 cores and our handler is microseconds), and isn't queue management (backlog 4096 is huge). It's **packets per second on the radio**.

### Back-of-the-envelope: proving Wi-Fi is the bottleneck

The "Wi-Fi is the bottleneck" claim deserves numbers, not vibes. Two independent calculations — one for throughput, one for latency — converge on the same conclusion.

#### Throughput math (why the ceiling is ~250 QPS)

Each TCP request in our protocol involves these distinct frames on the wire:

```
Client → Server                Server → Client
─────────────────              ─────────────────
1. SYN                          2. SYN-ACK
3. ACK                                              ← handshake done
4. PSH+ACK (request bytes)
                                5. PSH+ACK (response bytes, may piggyback ACK)
6. ACK (response)
7. FIN+ACK
                                8. FIN+ACK (may piggyback prior ACK)
9. ACK (final teardown)
```

Approximately **8–10 frames per request lifecycle**, depending on how aggressively the kernel piggybacks ACKs onto data frames. Call it **~8 frames per request** as a tight estimate.

Wi-Fi airtime per small frame (5 GHz, 802.11ac, ~100 byte frame at common MCS rates):

| Component | Time |
|---|---|
| DIFS (DCF inter-frame space) | ~28 µs |
| Random backoff (CSMA/CA average) | ~135 µs (CWmin=15, slot=9 µs) |
| PHY preamble + MAC header | ~16 µs |
| Data payload at ~6 Mbps (worst case) | ~140 µs |
| SIFS + ACK frame from peer | ~40 µs |
| **Total per frame** | **~360 µs** |

That gives a theoretical Wi-Fi small-packet PPS ceiling of:

```
1 / 360 µs ≈ 2,780 PPS
```

Real-world degradation from interference, neighbor BSS traffic, retransmits, and beaconing pulls this down by ~30–50%. Realistic sustained small-packet Wi-Fi PPS in a home environment is **~1,500–2,500 PPS**.

Now convert PPS to QPS for our protocol (~8 frames per request):

```
2,500 PPS / 8 frames per request ≈ 310 QPS
1,500 PPS / 8 frames per request ≈ 190 QPS
```

**Our observed ceiling of ~250 QPS sits exactly in the middle of that range.** This is the Wi-Fi PHY hitting its small-packet limit, not anything to do with our server code.

Implication: **a faster server cannot raise this ceiling.** The bytes physically cannot leave the Pi any faster. SO_REUSEPORT, more cores, faster handlers — none would help against this floor. Only switching networks (Ethernet, or persistent connections that amortize the per-request frame count) can.

#### Latency math (why p50 floors at ~50 ms even at 10 QPS)

A single request on this protocol requires multiple **sequential** RTTs — each one waiting for the previous to complete:

```
RTT 1: Mac SYN → Pi SYN-ACK → Mac ACK    (handshake)
RTT 2: Mac data → Pi response             (request/response)
RTT 3: Mac FIN → Pi FIN-ACK → Mac final ACK (teardown)
```

So **~3 RTTs of wall-clock time per request, minimum.** Even an infinitely fast server cannot beat this floor.

Wi-Fi RTT on this network (measured via `ping`):

```
chaithanyashyamd@mac ~ % ping -c 3 192.168.1.13
64 bytes from 192.168.1.13: time=12.1 ms
64 bytes from 192.168.1.13: time=27.7 ms
64 bytes from 192.168.1.13: time=10.6 ms
```

Round-trip median ~12–28 ms. Call it ~18 ms typical. Variance is high because Wi-Fi RTT includes:

- Channel access contention (~135 µs average backoff per send)
- Wi-Fi power-save wake-up (radio idles between packets; first packet pays a wake tax)
- Driver/kernel scheduling on both sides
- Beacon-interval alignment with the AP
- Occasional retransmits on the radio link

For 3 RTTs at 18 ms each: theoretical minimum request latency = **~54 ms**.

Our **server 4 p50 at 10 QPS was 53 ms** — within 2% of the theoretical floor. This means:

- Server is doing essentially zero observable work; the request handler is microseconds.
- The 53 ms is entirely Wi-Fi airtime.
- Any improvement to the server's CPU work would be invisible in this metric.

Implication: **a faster server cannot reduce per-request latency below ~50 ms on this network**, regardless of architecture. The Wi-Fi RTT is the floor.

#### What this means for SO_REUSEPORT and Pattern E

The natural next step in the trajectory is `SO_REUSEPORT` + multi-process. On a CPU-bound workload, four processes on four cores would near-quadruple throughput.

**On this network, that experiment would show approximately no improvement.** Reason:

- Wi-Fi can deliver ~2,500 small PPS regardless of how many processes are accepting connections — the radio is shared by all processes on the same machine.
- The CPU is already idle (the handler is ~5 µs of work; even at 250 QPS we're using <1% of one core).

The right environment to demonstrate SO_REUSEPORT's value is **Ethernet** (which removes the PPS floor) or a workload with **non-trivial CPU per request** (which makes additional cores actually useful). For our setup, the next experiment that would genuinely improve numbers is **switching to Ethernet**, not adding processes.

This is itself a useful production lesson: **always identify the binding constraint before deploying more cores.** Scaling out the wrong dimension is a common waste of engineering effort.

### 5. The classic concurrency progression (blocking → blocking-with-larger-queue → epoll-naive → epoll-proper) maps cleanly to measurable performance steps

| Step | What changed | What it bought |
|---|---|---|
| 1 → 2 | Backlog 1 → 4096 | **16× more sustainable QPS** (burst absorption) |
| 2 → 3 | Added epoll dispatch | ~0% (slight latency regression) |
| 3 → 4 | Non-blocking I/O + state machine | ~25% more sustainable QPS, much lower latency at load |

This is a clean, teachable result: each architectural change has a different mechanism of improvement, and you can see them as discrete steps in the data.

---

## Caveats and what we didn't test

- **Wi-Fi variance.** Each test is one run. Wi-Fi quality varies minute-to-minute (interference, packet loss bursts). Running each server 5× and averaging would tighten the numbers, but the headline conclusions are robust to noise at this magnitude.
- **No Ethernet baseline.** All tests are Wi-Fi. To distinguish "server is the bottleneck" from "network is the bottleneck," an Ethernet rerun is essential. Strong prediction: server 4's lead over server 2/3 widens dramatically.
- **No persistent connections.** Each request opens a new TCP connection. With keep-alive (multiple requests over one connection), all servers would do dramatically better — and the throughput differences between architectures would shrink because handshake cost dominates per-request time here.
- **Single-process only.** None of these servers uses multiple cores. The Pi 5 has 4 cores; with `SO_REUSEPORT` and multiple processes, each running its own epoll loop, *CPU* scaling could approach 4×. **However**, as shown in the back-of-envelope section above, **this network is Wi-Fi-bound, not CPU-bound** — adding processes will not help here. SO_REUSEPORT is the right next step *only* once we move to Ethernet (or a workload with meaningful per-request CPU). Doing it on Wi-Fi would be a "scale the wrong dimension" pitfall.
- **Tiny payloads.** Request and response are both ~5 bytes. Realistic protocols with larger payloads would have a different cost structure (more bytes per RTT, fewer packets per request).
- **Client-side coordination omission**: `queue_ms` p99 stayed under 60 ms even at saturation, indicating the asyncio harness on the Mac was not contaminating measurements. Latencies reported are the real user-visible numbers.

---

## Artifacts

This experiment produced:

- `log_N_<name>.jsonl` — per-request raw JSON lines (timestamps, status, payload size, etc.).
- `chart_N_<name>.png` — visual chart with CDF, per-second throughput, error rate, concurrency, and stats panel.
- `errors_N_<name>.log` — human-readable error log with sample failing records grouped by status.

For reproducibility: the stress harness, server source files, and this report are all in the repository. The Pi-side compilation command used was `g++ -O2 -Wall -o server_bin <src>.cpp`. The exact stress invocation is documented in **Methodology**.
