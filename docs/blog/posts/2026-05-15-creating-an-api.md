# The simple task of hosting an API 

Hello everyone, recently there was a situation where a classmate had a local service he built and I needed to access it. I will give more background later but the situation at hand is the following:
- My friend developed an online service where one can query information about a particular blockcahin entity. He is the owner of this service and is responsible for updates.
- I have a published user-facing dashboard that needed to call this service and surface this data to the users. I own the dashboard and am responsible for updates.


That's it. This is the abstracted view and its actually so fucking annoying how there exists no simple solution for a simple problem like this.


## Problem specifics

My friend and I were teammates for the CS6270 Intro to Blockchain course at Georgia Tech. 

###
Course project:
- [TBA Claude] add a brief summary of the course project and what it was and what we were trying to do.

### Tech stack
[TBA Claude] Rewrite this entire section if you want.
- My friend developed a python server. SQL lite database etc,.
- I used railway to deploy.
- Add more details of our tech stack. The AWS stack is not yet part of the stack because that was the solution.
- My friend developed a transaction replay engine where we could query all the **relevant** transactions performed by a wallet address.
- I published a live leaderboard that people could use to find out the rankings. One component of the dashboard was to track the total relevant transactions I used railway to host.
- [TBA Claude] "His server runs on an AWS EC2 box behind a security group that only allows his own IP inbound. Railway's outbound IPs are dynamic and not publishable, so we can't just have him whitelist mine. My dashboard cannot reach his service over the public internet." Remove this from here and move it to a different section. Its part of the solution and the problem we faced.

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
[TBA Claude] Rewrite this entire section if you want.


"He already had port 80 open on his AWS box for an unrelated reason." not true. He spun up an AWS instance just for this service.

"So I wrote a small reverse proxy in pure stdlib python that listens on :80 and forwards every request to his actual service on :8080 (which is the locked-down one). scp'd it onto his instance, ran it in a tmux session, and pointed my dashboard's `UPSTREAM_API_BASE` env var at his public IP on port 80." Yes but why did we have to write a reverse proxy?

Traffic flow:

```
Railway dashboard -> AWS:80 (proxy.py) -> 127.0.0.1:8080 (his service)
```

100 lines of python. No new dependencies, no SaaS, no convincing him to widen his security group, no negotiating SSH access. His locked-down service stays locked down. My dashboard gets its data live.

## References
- Railway
- Code: https://github.com/Dragonado/carnival-deal (proxy lives at `upstream-proxy/proxy.py`)
