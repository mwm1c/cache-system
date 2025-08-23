## Project Introduction
This project implements a thread-safe cache using multiple page replacement policies:
- LRU: Least Recently Used
- LFU: Least Frequently Used
- ARC: Adaptive Replacement Cache

For LRU and LFU policies, several optimizations have been made:

- LRU Optimizations:
    - LRU Sharding: Improves performance for high-concurrency access in multi-threaded environments
    - LRU-k: Prevents hot data from being evicted by cold data, reducing cache pollution

- LFU Optimizations:
    - LFU Sharding: Improves performance for high-concurrency access in multi-threaded environments
    - Maximum Average Access Frequency: Prevents old hot data from occupying the cache when it is no longer accessed

## System Environment
```
Ubuntu 22.04 LTS
```
## Build
Create a build folder and enter it:
```
mkdir build && cd build
```
Generate build files:
```
cmake ..
```
Build the project:
```
make
```
To clean up generated executables:
```
make clean
```

## Run
```
./main
```

## Test Results
The following chart compares cache hit rates for different cache policies:
(Note: The test code simulates real access scenarios as much as possible, but there are still differences from actual usage. Test results are for reference only.)
