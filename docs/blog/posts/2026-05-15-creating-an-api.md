# The simple task of hosting an API - Part 1

Hello everyone, recently there was a situation where a classmate had a local service he built and I needed to access it. I will give more background later but the situation at hand is the following:
- My friend developed an online service where one can query information about a particular blockcahin entity. He is the owner of this service and is responsible for updates.
- I have a published user-facing dashboard that needed to call this service and surface this data to the users. I own the dashboard and am responsible for updates.


That's it. This is the abstracted view and its actually so fucking annoying how there exists no simple solution for a simple problem like this.


## Problem specifics

My friend and I were teammates for the CS6270 Intro to Blockchain course at Georgia Tech. 

### Course project
[TBA Claude] Rewrite this before its too much technical stuff. People wont care about the exact games. Give a little bit more high level view.


The final assignment was Buzzcoin Carnival 2 on Sepolia testnet: complete a rubric across seven on-chain games (mining, reentrancy exploit, two-DEX arbitrage, KOTH, mayor voting, guess-the-number, and duels) and rack up at least 50 transactions. Noe and I co-built a paid service that used EIP-7702 delegation to execute the entire assignment on classmates' wallets for a fee, plus a public dashboard tracking every student's rubric progress live so prospective customers could see who had already taken the deal.

### Tech stack

**His side:**
- Stdlib python HTTP server (threaded `http.server`)
- SQLite for storage
- A scraper that fetched Sepolia blocks and traced every `[CALL]` frame inside each tx, attributing each call back to the EOA it should count for
- Two routes: `/<addr>/txn.txt` (per-wallet plain text list of relevant txs) and `/api/dashboard` (JSON aggregate across all wallets)

**My side:**
- Node.js / Express backend
- better-sqlite3 for storage
- WebSocket for live tx broadcasts to the frontend
- ethers v6 for Sepolia RPC calls (storage reads, delegation lookups, log replay)
- React + Vite frontend
- Railway as the host, single Node service that also serves the static frontend bundle

His service ran on his own machine while we developed. Mine was on Railway from day one.

### The actual constraint

To go live I needed his service reachable from my Railway backend so the dashboard could call it on every page load. He didn't want it open to the world, just open to me. AWS security groups normally handle this with an IP allowlist, but Railway's outbound IPs are dynamic and not publishable anywhere, so "just whitelist my server" was not on the table. His service was effectively behind a NAT from my perspective.

## Alternate solutions

### Batch job

Pros:
- Just send a .txt file periodically.
- No network handshake between us. He pushes to S3 or a public URL, I pull from it on a cron.

Cons:
- Not live.
- Data is as stale as the last sync. If a user opens the dashboard 5 minutes after activity, they see old numbers.
- I have to host the .txt files somewhere too, doubling storage and adding a second thing that can break.

### Clone the service locally

Pros:
- Zero network hops, zero firewall headaches.
- My dashboard works even if his box is offline.
- Latency is whatever SQLite gives me, which is microseconds.

Cons:
- I had to periodicially sync the code with the latest push.
- His service has its own scraping pipeline and database. Cloning isn't just the code, its running another instance of his entire infra.
- Defeats split ownership. If anything breaks I have to debug his stack instead of mine.

### 

## Chosen solution

He spun up an AWS EC2 instance specifically for this service and opened port 80 inbound to the world. His actual service binds to port 8080 (non-privileged port, that's how he developed it locally). So the public-facing port and the service port don't match. Two ways to fix that:

1. Reconfigure his service to bind directly to port 80.
2. Leave his service exactly as-is and stick a tiny reverse proxy in front that listens on :80 and forwards to :8080.

Option 1 needs root on the EC2 box (anything below port 1024 is privileged on Linux), modifying his code, and committing his service to run as root forever. Option 2 is a separate 100-line python file that does one thing and never touches his service.

I wrote the proxy in stdlib python only ([upstream-proxy/proxy.py](https://github.com/Dragonado/carnival-deal/blob/main/upstream-proxy/proxy.py)). It listens on `0.0.0.0:80` and forwards every incoming request verbatim to `127.0.0.1:8080`. scp'd onto his instance, ran in a tmux session under `sudo`, pointed my dashboard's `UPSTREAM_API_BASE` env var at his public IP on port 80. Done.

Traffic flow:

```
Railway dashboard -> AWS:80 (proxy.py, runs as root) -> 127.0.0.1:8080 (his service, runs as ec2-user)
```

No new dependencies. No SaaS. No widening his security group beyond the one port already open. No SSH tunneling, no Cloudflare workers, no negotiating IP allowlists with Railway. His service stays exactly as he wrote it. My dashboard gets its data live.

## References
- Railway: https://railway.com
- Code: https://github.com/Dragonado/carnival-deal (proxy lives at `upstream-proxy/proxy.py`)
- Dashboard: https://carnival-deal-production.up.railway.app
