#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "CachePolicy.h"

namespace mwm1cCache
{
    template <typename Key, typename Value>
    class LfuCache;

    template <typename Key, typename Value>
    class FreqList
    {
    private:
        struct Node
        {
            int freq;
            Key key;
            Value value;
            std::weak_ptr<Node> prev;
            std::shared_ptr<Node> next;
            Node()
                : freq(1), next(nullptr) {}
            Node(Key key, Value value)
                : freq(1), key(key), value(value), next(nullptr) {}
        };
        using NodePtr = std::shared_ptr<Node>;
        int freq_;
        NodePtr head_;
        NodePtr tail_;

    public:
        explicit FreqList(int n)
            : freq_(n)
        {
            head_ = std::make_shared<Node>();
            tail_ = std::make_shared<Node>();
            head_->next = tail_;
            tail_->prev = head_;
        }
        bool isEmpty() const
        {
            return head_->next == tail_;
        }
        void addNode(NodePtr node)
        {
            if (!node || !head_ || !tail_)
                return;
            node->prev = tail_->prev;
            node->next = tail_;
            tail_->prev.lock()->next = node;
            tail_->prev = node;
        }
        void removeNode(NodePtr node)
        {
            if (!node || !head_ || !tail_)
                return;
            if (node->prev.expired() || !node->next)
                return;
            auto prev = node->prev.lock();
            prev->next = node->next;
            node->next->prev = prev;
            node->next = nullptr;
        }
        NodePtr getFirstNode() const
        {
            return head_->next;
        }
    };

    template <typename Key, typename Value>
    class LfuCache : public CachePolicy<Key, Value>
    {
    public:
        using Node = typename FreqList<Key, Value>::Node;
        using NodePtr = std::shared_ptr<Node>;
        using NodeMap = std::unordered_map<Key, NodePtr>;

        LfuCache(int cap, int maxAvgNum = 1000000)
            : capacity_(cap), minFreq_(INT8_MAX),
              , maxAvgNum_(maxAvgNum), curAvgNum_(0), curTotalNum_(0)
        {
        }
        ~LfuCache() override = default;
        void put(Key key, Value value) override
        {
            if (!capacity_)
                return;
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                it->second->value = value;
                getInternal(it->second, value);
                return;
            }
            putInternal(key, value);
        }
        bool get(Key key, Value &value) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap.end())
            {
                getInternal(it->second, value);
                return true;
            }
            return false;
        }
        Value get(Key key) override
        {
            Value value;
            get(key, value);
            return value;
        }
        void purge()
        {
            nodeMap_.clear();
            for (auto& pair: freqToFreqList_)
            {
                delete pair.second;
            }
            freqToFreqList_.clear();
        }

    private:
        void putInternal(Key key, Value value)
        {
            if (nodeMap_.size() == capacity_)
            {
                kickOut();
            }
            NodePtr node = std::make_shared<Node>(key, value);
            nodeMap_[key] = node;
            addToFreqList(node);
            addFreqNum();
            minFreq_ = std::min(minFreq_, 1);
        }
        void getInternal(NodePtr node, Value &value)
        {
            value = node->value;
            removeFromFreqList(node);
            ++node->freq;
            addToFreqList(node);
            /**
             * If the access frequency of the current node equals minFreq+1 and
             * its predecessor list is empty, it means the freqToFreqList_[node->freq - 1] list
             * is empty due to the migration of the node,
             * and the minimum access frequency needs to be updated
             */
            if (node->freq - 1 == minFreq_ && freqToFreqList_[node->freq - 1]->isEmpty())
            {
                ++minFreq_;
            }
            // update metadata
            addFreqNum();
        }
        void kickOut()
        {
            NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
            removeFromFreqList(node);
            nodeMap_.erase(node->key);
            decreaseFreqNum(node->freq);
        }
        void removeFromFreqList(NodePtr node)
        {
            if (!node)
            {
                return;
            }
            auto freq = node->freq;
            freqToFreqList_[freq]->removeNode(node);
        }
        void addToFreqList(NodePtr node)
        {
            if (!node)
                return;
            auto freq = node->freq;
            // check if freqList exist
            if (freqToFreqList_.find(node->freq) == freqToFreqList_.end())
            {
                // if don't exist, create it
                freqToFreqList_[node->freq] = new FreqList<Key, Value>(node->freq);
            }
            freqToFreqList_[freq]->addNode(node);
        }
        void addFreqNum()
        {
            ++curTotalNum_;
            if (nodeMap_.empty())
            {
                curAvgNum_ = 0;
            }
            else
            {
                curAvgNum_ = curTotalNum_ / nodeMap_.size();
            }
            if (curAvgNum_ > maxAvgNum_)
            {
                handleOverMaxAvgNum();
            }
        }
        void decreaseFreqNum(int num)
        {
            curTotalNum_ -= num;
            if (nodeMap_.empty())
            {
                curAvgNum_ = 0;
            }
            else
            {
                curAvgNum_ = curTotalNum_ / nodeMap_.size();
            }
        }
        /**
         * The "frequency inflation" of hot data can severely impact the fairness of LFU eviction,
         * keeping cold data constantly on the verge of being evicted.
         * Therefore, it is necessary to periodically compress the frequency to ensure the caching
         * effectiveness and healthy distribution of LFU.
         */
        void handleOverMaxAvgNum()
        {
            if (nodeMap_.empty())
                return;
            for (auto it = nodeMap_.begin(); it != nodeMap_.end(); ++it)
            {
                if (!it->second)
                {
                    continue;
                }
                NodePtr node = it->second;
                removeFromFreqList(node);
                node->freq -= maxAvgNum_ / 2;
                if (node->freq < 1)
                {
                    node->freq = 1;
                }
                addToFreqList(node);
            }
            updateMinFreq();
        }
        void updateMinFreq()
        {
            minFreq_ = INT8_MAX;
            for (const auto &pair : freqToFreqList_)
            {
                if (pair.second && !pair.second->isEmpty())
                {
                    minFreq_ = std::min(minFreq_, pair.first);
                }
            }
            if (minFreq_ == INT8_MAX)
            {
                minFreq_ = 1;
            }
        }

    private:
        int capacity_;
        int minFreq_;
        int maxAvgNum_;
        int curAvgNum_;
        int curTotalNum_;
        std::mutex mutex_;
        NodeMap nodeMap_;
        std::unordered_map<int, FreqList<Key, Value> *> freqToFreqList_;
    };

    template <typename Key, typename Value>
    class HashLfuCache
    {
    public:
        HashLfuCache(size_t cap, int sliceNum, int maxAvgNum = 10)
            : capacity_(cap), sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
        {
            size_t sliceSize = std::ceil(cap / static_cast<double>(sliceNum_));
            for (int i = 0; i < sliceNum; ++ i)
            {
                lfuSliceCaches_.emplace_back(new LfuCache<Key, Value>(sliceSize, maxAvgNum));
            }
        }
        void put(Key key, Value value)
        {
            size_t sliceIndex = Hash(key) % sliceNum_;
            lfuSliceCaches_[sliceIndex]->put(key, value);
        }
        bool get(Key key, Value &value)
        {
            size_t sliceIndex = Hash(key) % sliceNum_;
            return lfuSliceCaches_[sliceIndex]->get(key, value);
        }
        Value get(Key key)
        {
            Value value{};
            get(key, value);
            return value;
        }
        void purge()
        {
            for (auto &lfuSliceCache : lfuSliceCaches_)
            {
                lfuSliceCache->purge();
            }
        }

    private:
        size_t Hash(Key key)
        {
            std::hash<Key> hashFunc;
            return hashFunc(key);
        }

    private:
        size_t capacity_;
        int sliceNum_;
        std::vector<std::unique_ptr<LfuCache<Key, Value>>> lfuSliceCaches_;
    };
}