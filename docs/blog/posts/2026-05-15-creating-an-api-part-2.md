---
date: 2026-05-15
draft: true
---

# The simple task of hosting an API - Part 2 [TBA]

## Background

Check out part 1 for the context of this post.

I used to work in Google as a SWE before and I always took infra work like hosting an API for granted. If I needed to serve a service to someone in Google, all I had to do was spin up a [Boq node](https://www.reddit.com/r/programming/comments/s21wti/comment/hsculqs/?utm_source=share&utm_medium=web3x&utm_name=web3xcss&utm_term=1&utm_content=share_button) (the internal microservice framework) and all the boilerplate work would be taken care of. I just needed to define the IO parameters, write the service part and send the endpoint (provided by the internal load balancer) to my colleague who wanted the service. 

Of course its only this simple for experimental work, for production services an engineer has to add unit tests, regression tests (if necessary), end-to-end tests, functional tests, load tests, etc.

Joining Google straight out of undergrad was an opportunity that I will forever be grateful for. I had immense growth both professionally and personally. I learnt a great deal of software engineering and how production code is supposed to be shipped that can handle planet-scale. So while I learnt a great deal of **using** planet-scale infra, I was severely lacking in technical abilities of actually **building** a planet-scale infra.

So I joined back academia. Case in point from another Google engineer (and friend) https://mishal23.github.io/back-to-academia/. We share the same sentiment but reached different conclusions. He chose to stay in Google and pursued an online masters from Georgia Tech part time. I chose to leave Google and pursued an in-person masters degree from Georgia Tech full time. Note to the reader: There is no right/wrong answer in our decisions here.

So now I want to master this concept and make sure that I never get stumped again when someone asks me "hey can you host this service so I can query it?".


## References

- [Beej](https://beej.us/guide/bgnet/)