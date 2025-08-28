#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>

#include "CachePolicy.h"
#include "LFUCache.h"
#include "LRUCache.h"
#include "ArcCache/ArcCache.h"

class Timer
{
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed()
    {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

void printResults(const std::string &testName, int capacity,
                  const std::vector<int> &get_operations,
                  const std::vector<int> &hits)
{
    std::cout << "=== " << testName << " Summary ===" << std::endl;
    std::cout << "Cache Capacity: " << capacity << std::endl;

    std::vector<std::string> names;
    if (hits.size() == 3)
    {
        names = {"LRU", "LFU", "ARC"};
    }
    else if (hits.size() == 4)
    {
        names = {"LRU", "LFU", "ARC", "LRU-K"};
    }
    else if (hits.size() == 5)
    {
        names = {"LRU", "LFU", "ARC", "LRU-K", "LFU-Aging"};
    }

    for (size_t i = 0; i < hits.size(); ++i)
    {
        double hitRate = 100.0 * hits[i] / get_operations[i];
        std::cout << (i < names.size() ? names[i] : "Algorithm " + std::to_string(i + 1))
                  << " - Hit Rate: " << std::fixed << std::setprecision(2)
                  << hitRate << "% ";
        std::cout << "(" << hits[i] << "/" << get_operations[i] << ")" << std::endl;
    }

    std::cout << std::endl;
}

void testHotDataAccess()
{
    std::cout << "\n=== Test Scenario 1: Hot Data Access ===" << std::endl;

    const int CAPACITY = 20;
    const int OPERATIONS = 500000;
    const int HOT_KEYS = 20;
    const int COLD_KEYS = 5000;

    mwm1cCache::LruCache<int, std::string> lru(CAPACITY);
    mwm1cCache::LfuCache<int, std::string> lfu(CAPACITY);
    mwm1cCache::ArcCache<int, std::string> arc(CAPACITY);

    std::array<mwm1cCache::CachePolicy<int, std::string> *, 3> caches = {&lru, &lfu, &arc};
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);

    std::random_device rd;
    std::mt19937 gen(rd());

    for (int i = 0; i < caches.size(); ++i)
    {
        for (int key = 0; key < HOT_KEYS; ++key)
        {
            std::string value = "value" + std::to_string(key);
            caches[i]->put(key, value);
        }

        for (int op = 0; op < OPERATIONS; ++op)
        {
            bool isPut = (gen() % 100 < 30);
            int key;
            if (gen() % 100 < 70)
            {
                key = gen() % HOT_KEYS;
            }
            else
            {
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }

            if (isPut)
            {
                std::string value = "value" + std::to_string(key) + "_v" + std::to_string(op % 100);
                caches[i]->put(key, value);
            }
            else
            {
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result))
                {
                    hits[i]++;
                }
            }
        }
    }

    printResults("Host Data Access Test", CAPACITY, get_operations, hits);
}

void testLoopPattern()
{
    std::cout << "\n=== Test Scenario 2: Loop Scan ===" << std::endl;

    const int CAPACITY = 50;
    const int LOOP_SIZE = 500;
    const int OPERATIONS = 200000;

    mwm1cCache::LruCache<int, std::string> lru(CAPACITY);
    mwm1cCache::LfuCache<int, std::string> lfu(CAPACITY);
    mwm1cCache::ArcCache<int, std::string> arc(CAPACITY);

    std::array<mwm1cCache::CachePolicy<int, std::string> *, 3> caches = {&lru, &lfu, &arc};
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);

    std::random_device rd;
    std::mt19937 gen(rd());

    for (int i = 0; i < caches.size(); ++i)
    {
        for (int key = 0; key < LOOP_SIZE / 5; ++key)
        {
            std::string value = "loop" + std::to_string(key);
            caches[i]->put(key, value);
        }

        int current_pos = 0;
        for (int op = 0; op < OPERATIONS; ++op)
        {
            bool isPut = (gen() % 100 < 20);
            int key;
            if (op % 100 < 60)
            {
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            }
            else if (op % 100 < 90)
            {
                key = gen() % LOOP_SIZE;
            }
            else
            {
                key = LOOP_SIZE + (gen() % LOOP_SIZE);
            }

            if (isPut)
            {
                std::string value = "loop" + std::to_string(key) + "_v" + std::to_string(op % 100);
                caches[i]->put(key, value);
            }
            else
            {
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result))
                {
                    hits[i]++;
                }
            }
        }
    }

    printResults("Loop Scan Test", CAPACITY, get_operations, hits);

}

void testWorkloadShift()
{
    std::cout << "\n=== Test Scenario 3: Workload Shift ===" << std::endl;

    const int CAPACITY = 30;
    const int OPERATIONS = 80000;
    const int PHASE_LENGTH = OPERATIONS / 5;

    mwm1cCache::LruCache<int, std::string> lru(CAPACITY);
    mwm1cCache::LfuCache<int, std::string> lfu(CAPACITY);
    mwm1cCache::ArcCache<int, std::string> arc(CAPACITY);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::array<mwm1cCache::CachePolicy<int, std::string> *, 3> caches = {&lru, &lfu, &arc};
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);

    for (int i = 0; i < caches.size(); ++i)
    {
        for (int key = 0; key < 30; ++key)
        {
            std::string value = "init" + std::to_string(key);
            caches[i]->put(key, value);
        }

        for (int op = 0; op < OPERATIONS; ++op)
        {
            int phase = op / PHASE_LENGTH;
            int putProbability;
            switch (phase)
            {
            case 0:
                putProbability = 15;
                break;
            case 1:
                putProbability = 30;
                break;
            case 2:
                putProbability = 10;
                break;
            case 3:
                putProbability = 25;
                break;
            case 4:
                putProbability = 20;
                break;
            default:
                putProbability = 20;
            }

            bool isPut = (gen() % 100 < putProbability);
            int key;
            if (op < PHASE_LENGTH)
            {
                key = gen() % 5;
            }
            else if (op < PHASE_LENGTH * 2)
            {
                key = gen() % 400;
            }
            else if (op < PHASE_LENGTH * 3)
            {
                key = (op - PHASE_LENGTH * 2) % 100;
            }
            else if (op < PHASE_LENGTH * 4)
            {
                int locality = (op / 800) % 5;
                key = locality * 15 + (gen() % 15);
            }
            else
            {
                int r = gen() % 100;
                if (r < 40)
                {
                    key = gen() % 5;
                }
                else if (r < 70)
                {
                    key = 5 + (gen() % 45);
                }
                else
                {
                    key = 50 + (gen() % 350);
                }
            }

            if (isPut)
            {
                std::string value = "value" + std::to_string(key) + "_p" + std::to_string(phase);
                caches[i]->put(key, value);
            }
            else
            {
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result))
                {
                    hits[i]++;
                }
            }
        }
    }

    printResults("Workload Shift Test", CAPACITY, get_operations, hits);
}

int main()
{
    testHotDataAccess();
    testLoopPattern();
    testWorkloadShift();
    return 0;
}
