#pragma once

#include "../CachePolicy.h"
#include "ArcLruPart.h"
#include "ArcLfuPart.h"
#include <memory>

namespace mwm1cCache
{
    template <typename Key, typename Value>
    class ArcCache : public CachePolicy<Key, Value>
    {
    public:
        explicit ArcCache(size_t cap = 10, size_t transformThreshold = 2)
            : capacity_(cap), transformThreshold_(transformThreshold)
        {
        }

        ~ArcCache() override = default;

        void put(Key key, Value value) override
        {
            // decide whether to adjust the capacity of LRU/LFU
            checkGhostCaches(key);
            // check if LfuPart contains the key
            bool inLfu = lfuPart_->contain(key);
            // update LruPart Caches
            lruPart_->put(key, value);
            // update LfuPart if LfuPart contains the key
            if (inLfu)
            {
                lfuPart_->put(key, value);
            }
        }

        bool get(Key key, Value &value) override
        {
            checkGhostCaches(key);
            bool shouldTransform = false;
            
            if (lruPart_->get(key, value, shouldTransform))
            {
                if (shouldTransform)
                {
                    lfuPart_->put(key, value);
                }
                return true;
            }
            return lfuPart_->get(key, value);
        }

        Value get(Key key) override
        {
            Value value{};
            get(key, value);
            return value;
        }

    private:
        bool checkGhostCaches(Key key)
        {
            bool inGhost = false;
            if (lruPart_->checkGhost(key))
            {
                if (lfuPart_->decreaseCapacity())
                {
                    lruPart_->increaseCapacity();
                }
            }
            else if (lfuPart_->checkGhost(key))
            {
                if (lruPart_->decreaseCapacity())
                {
                    lfuPart_->increaseCapacity();
                }
                inGhost = true;
            }
            return inGhost;
        }

    private:
        size_t capacity_;
        size_t transformThreshold_;
        std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
        std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
    };
}