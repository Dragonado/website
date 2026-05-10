---
layout: page
title: Projects
permalink: /projects/
---

A selection of things I've built. Code lives at [github.com/Dragonado](https://github.com/Dragonado).

### Buzzcoin on Ethereum Sepolia
*Solidity, Foundry, C++, Node.js, React, SQLite — Apr 2026*

Two **EIP-7702 delegate contracts** on the Sepolia testnet that ran an entire CS6270 course rubric inside opted-in EOAs. Bundled offline authorization tuples into **type-4 transactions** so payouts atomically routed delegator → operator within a single tx.

Alongside the delegates I wrote a **reentrancy attacker** against a DAO-style exploit that drained the cap in one tx via the fallback hook, and a **mempool sandwich scanner** that polled pending Sepolia txs, simulated them against a local Foundry fork, and flagged profit-after-gas front-run / back-run pairs across two constant-product AMMs — including multi-hop **DEX arbitrage cycles** on oracle-induced imbalances. A live indexing dashboard tied it together with three ingestion paths: log replay, direct storage polling, and inner `[CALL]`-frame attribution to capture EIP-7702 delegated calls.

### Corrent — BitTorrent client from scratch
*Rust — Oct 2025*

A memory-safe BitTorrent client covering the full protocol stack: Bencode serialization (custom **recursive-descent decoder** for all wire-format types), tracker HTTP API, and peer-to-peer TCP communication with handshake negotiation, choke / unchoke state machine, and sequential piece request / response logic against live peers. SHA-1 piece integrity verification against the torrent manifest for every chunk.

### CodePal — VSCode extension for competitive programmers
*TypeScript — ongoing*

[Marketplace](https://marketplace.visualstudio.com/items?itemName=IEEE-NITK.codepal) · One of the lead maintainers. Testcase fetching, parallel runs against a brute force, stress testing, snippets — the things you actually need during a contest. **6000+** users worldwide.

### Competitive programming problem sets
*Various judges — 2020–present*

[Problem repo](https://github.com/Dragonado/my_CP_problems) collects prompts, editorials, and reference solutions for problems I've set for **ICPC 2022 regional rounds**, **CodeChef**, and **CodeDrills**.
