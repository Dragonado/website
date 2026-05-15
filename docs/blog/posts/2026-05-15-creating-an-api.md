# The simple task of hosting an API 

Hello everyone, recently there was a situation where a classmate had a local service he built and I needed to access it. I will give more background later but the situation at hand is the following:
- My friend developed an online service where one can query information about a particular blockcahin entity. He is the owner of this service and is responsible for updates.
- I have a published user-facing dashboard that needed to call this service and surface this data to the users. I own the dashboard and am responsible for updates.


That's it. This is the abstracted view and its actually so fucking annoying how there exists no simple solution for a simple problem like this.


## Problem specifics

My friend and I were teammates for the CS6270 Intro to Blockchain course at Georgia Tech. 

Tech stack:
- My friend developed a python server.
- I used railway to deploy.

- My friend developed a transaction replay engine where we could query all the **relevant** transactions performed by a wallet address.
- I published a live leaderboard that people could use to find out the rankings. One component of the dashboard was to track the total relevant transactions I used railway to host.
- [TBA by Claude]

## Alternate solutions

### Batch job

Pros:
- Just send a .txt file periodically.
- [TBA by Claude]

Cons:
- Not live.
- [TBA by Claude]

### Clone the service locally

Pros:
- [TBA by Claude]


Cons:
- I had to periodicially sync the code with the latest push.
- [TBA by Claude]

### 

## Chosen solution

- [TBA by Claude]

## References
- [TBA by Claude]