# The simple task of hosting an API - Part 2 [TBA]

## Background

Check out part 1 for the context of this post.

I used to work in Google as a SWE before and I always took infra work like this for granted. If I needed to serve a service to someone in Google, all I had to do was spin up a boq composite node [TBA: link to what boq is] and all the boilerplate work would be taken care of. I just needed to write the API part and send the blade [TBA: link to what blade is] address to my colleague who wanted the service. 

Of course this is experimental work, for production services I had to add unit test, regression tested (if necessary), end-to-end tests, functional tests, load tests, etc,.

So now I want to master this concept and make sure that I never get confused again when someone asks me "hey can you host this service so I can query it?".