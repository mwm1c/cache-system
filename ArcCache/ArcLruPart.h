#pragma once

#include "ArcCacheNode.h"
#include <unordered_map>
#include <mutex>

namespace mwm1cCache
{
    template <typename Key, typename Value>
    class ArcLruPart
    {
    public:
        using NodeType = ArcNode<Key, Value>;
        using NodePtr = std::shared_ptr<NodeType>;
        using NodeMap = std::unordered_map<Key, NodePtr>;

        explicit ArcLruPart(size_t cap, size_t transformThreshold)
            : capacity_(cap), ghostCapacity_(cap), transformThreshold_(transformThreshold)
        {
            initializeLists();
        }

        bool put(Key key, Value value)
        {
            if (!capacity_)
            {
                return false;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = mainCache_.find(key);
            /**
             * if mainCache contains key, update value and move node to front
             * else add new node to front of mainCache
             */
            if (it != mainCache_.end())
            {
                return updateExistingNode(it->second, value);
            }
            return addNewNode(key, value);
        }

        bool get(Key key, Value &value, bool &shouldTransform)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = mainCache_.find(key);
            if (it == mainCache_.end())
            {
                shouldTransform = updateNodeAccess(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }

        bool checkGhost(Key key)
        {
            auto it = ghostCache_.find(key);
            if (it != ghostCache_.end())
            {
                removeFromGhost(it->second);
                ghostCache_.erase(it);
                return true;
            }
            return false;
        }

        void increaseCapacity()
        {
            ++capacity_;
        }

        bool decreaseCapacity()
        {
            if (capacity_ <= 0)
            {
                return false;
            }
            if (mainCache_.size() == capacity_)
            {
                evictLeastRecent();
            }
            --capacity_;
            return true;
        }

    private:
        void initializeLists()
        {
            mainHead_ = std::make_shared<NodeType>();
            mainTail_ = std::make_shared<NodeType>();
            mainHead_->next_ = mainTail_;
            mainTail_->prev_ = mainHead_;

            ghostHead_ = std::make_shared<NodeType>();
            ghostTail_ = std::make_shared<NodeType>();
            ghostHead_->next_ = ghostTail_;
            ghostTail_->prev_ = ghostHead_;
        }

        bool updateExistingNode(NodePtr node, const Value &value)
        {
            node->setValue(value);
            moveToFront(node);
            return true;
        }

        bool addNewNode(const Key &key, const Value &value)
        {
            // if mainCache is at capacity, evict least recently used node in mainCache
            if (mainCache_.size() >= capacity_)
            {
                evictLeastRecent();
            }
            NodePtr newNode = std::make_shared<NodeType>(key, value);
            // map node in hashmap
            mainCache_[key] = newNode;
            addToFront(newNode);
            return true;
        }

        bool updateNodeAccess(NodePtr node)
        {
            moveToFront(node);
            node->incrementAccessCount();
            return node->getAccessCount() >= transformThreshold_;
        }

        void moveToFront(NodePtr node)
        {
            if (!node->prev_.expired() && node->next_)
            {
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_ = node->prev_;
                node->next_ = nullptr;
            }
            addToFront(node);
        }

        void addToFront(NodePtr node)
        {
            node->next_ = mainHead_->next_;
            node->prev_ = mainHead_;
            mainHead_->next_->prev_ = node;
            mainHead_->next_ = node;
        }

        void evictLeastRecent()
        {
            NodePtr leastRecent = mainTail_->prev_.lock();
            if (!leastRecent || leastRecent == mainHead_)
            {
                return;
            }
            // move node from mainCache to ghostCache
            removeFromMain(leastRecent);
            if (ghostCache_.size() >= ghostCapacity_)
            {
                removeOldestGhost();
            }
            addToGhost(leastRecent);
            mainCache_.erase(leastRecent->getKey());
        }

        void removeFromMain(NodePtr node)
        {
            if (!node->prev_.expired() && node->next_)
            {
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_ = node->prev_;
                node->next_ = nullptr;
            }
        }

        void removeFromGhost(NodePtr node)
        {
            if (!node->prev_.expired() && node->next_)
            {
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_ = node->prev_;
                node->next_ = nullptr;
            }
        }

        void addToGhost(NodePtr node)
        {
            // reset access count
            node->accessCount_ = 1;
            node->next_ = ghostHead_->next_;
            node->prev_ = ghostHead_;
            ghostHead_->next_->prev_ = node;
            ghostHead_->next_ = node;
            // map node in ghost hashmap and remove from main hashmap
            ghostCache_[node->getKey()] = node;
        }

        void removeOldestGhost()
        {
            NodePtr oldestGhost = ghostTail_->prev_.lock();
            if (!oldestGhost || oldestGhost == ghostHead_)
            {
                return;
            }
            removeFromGhost(oldestGhost);
            ghostCache_.erase(oldestGhost->getKey());
        }

    private:
        size_t capacity_;
        size_t ghostCapacity_;
        size_t transformThreshold_;
        std::mutex mutex_;
        // key -> ArcNode
        NodeMap mainCache_;
        NodeMap ghostCache_;
        // main linkedList
        NodePtr mainHead_;
        NodePtr mainTail_;
        // ghost linkedList
        NodePtr ghostHead_;
        NodePtr ghostTail_;
    };
}