# MarketFlow

**Original project date (from memory):** June 2025  
**Published on GitHub:** November 6, 2026

A crossed quote shows up when the best bid meets or exceeds the best ask. MarketFlow follows those prices as updates arrive and flags crossed quotes and stale messages. One writer updates each book. A reader can fetch both quantities at a given price with one atomic load.

## Try it

From Linux or Ubuntu in WSL, install GNU Make and a C++17 compiler, then run `make test`. For the longer replay, run `make benchmark`. Both commands print JSON.

## What the replay showed

The balanced workload touched all 25,000 price levels across 100 symbols. The recorded run processed 100 million synthetic updates at 26.97 million updates per second. Its internal alert latency at the 99th percentile was 35.2 microseconds across 705 sampled alerts. Network transport falls outside the timer. The full run is in `results/marketflow_balanced.json`.

Developed with AI assistance.
