#pragma once

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "CachePolicy.h"

namespace mwm1cCache
{
    template <typename Key, typename Value>
    class LruCache;

    template <typename Key, typename Value>
    class LruNode
    {
    private:
        Key key_;
        Value value_;
        size_t accessCount_;
        std::weak_ptr<LruNode<Key, Value>> prev_;
        std::shared_ptr<LruNode<Key, Value>> next_;

    public:
        LruNode(Key key, Value value)
            : key_(key), value_(value), accessCount(1)
        {
        }
        Key getKey() const { return key_ };
        Value getValue() const { return value_ };
        void setValue(const Value &value) { value_ = value };
        size_t getAccessCount() const { return accessCount_ };
        void incrementAccessCount() { ++accessCount_ };

        friend class LruCache<Key, Value>;
    };

    // version 1
    template <typename Key, typename Value>
    class LruCache : public CachePolicy<Key, Value>
    {
    public:
        using LruNodeType = LruNode<Key, Value>;
        using NodePtr = std::shared<LruNodeType>; // be careful
        using NodeMap = std::unordered_map<Key, NodePtr>;
        LruCache(int cap) : capacity_(cap)
        {
            initializeList();
        }
        ~LruCache() override = default;
        void put(Key key, Value value) override
        {
            if (capacity_ <= 0)
                return;
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                /**
                 * If the key is in the current container, update the value and call the get method,
                 * indicating that the data has just been accessed.
                 */
                updateExistingNode(it->second, value);
                return;
            }
            addNewNode(key, value);
        }
        bool get(Key key, Value &value) override
        {
            std::lock_guard(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap.end())
            {
                moveToMostRecent(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }
        Value get(Key key) override
        {
            Value value{};
            get(key, value);
            return value;
        }
        void remove(Key key)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                removeNode(it->second);
                nodeMap_.erase(it);
            }
        }

    private:
        void initialize()
        {
            dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
            dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
            dummyHead_->next = dummyTail_;
            dummyTail_->next = dummyHead_;
        }
        void updateExistingNode(NodePtr node, const Value &value)
        {
            node->setValue(value);
            moveToMostRecent(node);
        }
        void addNewNode(const Key &key, const Value &value)
        {
            if (nodeMap_.size() >= capacity_)
            {
                evictLeastRecent();
            }
            // NodePtr newNode = std::make_shared<NodePtr>(Key(key), Value(value));
            NodePtr newNode = std::make_shared<LruNodeType>(key, value);
            insertNode(newNode);
            nodeMap_[key] = newNode;
        }
        void moveToMostRecent(NodePtr node)
        {
            removeNode(node);
            insertNode(node);
        }
        void removeNode(NodePtr node)
        {
            if (!node->prev.expired() && node->next_)
            {
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_ = prev;
                node->next_ = nullptr;
            }
        }
        // insert node to tail of linkedlist
        void insertNode(NodePtr node)
        {
            node->next_ = dummyTail_;
            node->prev_ = dummyTail_->prev_;
            dummyTail_->prev_.lock()->next = node;
            dummyTail_->prev_ = node;
        }
        void evictLeastRecent()
        {
            NodePtr leastRecent = dummyHead_->next_;
            removeNode(leastRecent);
            nodeMap_.erase(leastRecent->getKey());
        }
        int capacity_;
        NodeMap nodeMap_;
        std::mutex mutex_;
        NodePtr dummyHead_;
        NodePtr dummyTail_;
    };

    // version 2
    template <typename Key, typename Value>
    class LruKCache : public LruCache<Key, Value>
    {
    public:
        LruKCache(int cap, int historyCap, int k)
            : LruCache<Key, Value>(cap), historyList_(std::make_unique<LruCache<Key, size_t>>(historyCap)), k_(k) {}
        Value get(Key key)
        {
            Value value{};
            bool inMainCache = LruCache<Key, Value>::get(key, value);
            // fetch and update access history count
            size_t historyCount = historyList_->get(key);
            ++historyCount;
            historyList_->put(key, historyCount);
            // return directly if data in main-cache
            if (inMainCache)
            {
                return value;
            }
            // if data isn't in main-cache, but access reach to k
            if (historyCount >= k_)
            {
                auto it = historyValueMap_.find(key);
                if (it != historyValueMap_.end())
                {
                    // have history record, move it to main-cache
                    Value storedValue = it->second;
                    // remove item from history record
                    historyList_->remove(key);
                    historyValueMap_.erase(it);
                    // move to main-cache
                    LruCache<Key, Value>::put(key, storedValue);
                    return storedValue;
                }
                // dont have history record, return default value;
            }
            return value;
        }
        void put(Key key, Value value)
        {
            // check out if value in main-cache
            Value existingValue{};
            bool inMainCache = LruCache<Key, Value>::get(key, existingValue);
            if (inMainCache)
            {
                LruCache<Key, Value>::put(key, value);
                return;
            }
            // fetch and update history record
            size_t historyCount = historyList_->get(key);
            ++historyCount;
            historyList_->put(key, historyCount);
            // save key:value to history record map
            historyValueMap_[key] = value;
            // check if history record item reach to k
            if (historyCount >= k_)
            {
                historyList_->remove(key);
                historyValueMap_.erase(key);
                LruCache<Key, Value>::put(key, value);
            }
        }

    private:
        // criteria for entering the cache queue
        int k_;
        // Access Data History (value represents the number of visits)
        std::unique_ptr<LruCache<Key, size_t>> historyList_;
        // Data values that have not reached k accesses
        std::unordered_map<Key, Value> historyValueMap_;
    };

    // version 3
    template <typename Key, typename Value>
    class HashLruCaches
    {
    public:
        HashLruCaches(size_t cap, int sliceNum)
            : capacity_(cap), sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
        {
            size_t sliceSize = std::ceil(cap / static_cast<double>(sliceNum_));
            for (int i = 0; i < sliceNum; ++i)
            {
                lruSliceCaches.emplace_back(new LruCache<Key, Value>(sliceNum_));
            }
        }
        void put(Key key, Value value)
        {
            size_t sliceIndex = Hash(key) % sliceNum_;
            lruSliceCaches[sliceIndex]->put(key, value);
        }
        bool get(Key key, value &value)
        {
            size_t sliceIndex = Hash(key) % sliceNum_;
            return lruSliceCaches[sliceIndex]->get(key, value);
        }
        Value get(Key key)
        {
            Value value{};
            get(key, value);
            return value;
        }

    private:
        size_t Hash(Key key)
        {
            std::hash<Key> hashFunc;
            return hashFunc(key);
        }
        size_t capacity_;
        int sliceNum_;
        std::vector<std::unique_ptr<LruCache<Key, Value>>> lruSliceCaches;
    };
}