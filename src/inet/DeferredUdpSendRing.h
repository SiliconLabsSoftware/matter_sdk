/*
 *
 *    Copyright (c) 2020-2026 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 * Fixed-capacity FIFO ring used by DeferredUdpSendQueueLwIP. Header-only so
 * queue mechanics can be covered by host unit tests without LwIP.
 */

#pragma once

#include <lib/support/FixedBuffer.h>

#include <cstddef>
#include <utility>

namespace chip {
namespace Inet {

template <typename Slot, size_t Capacity>
struct DeferredUdpSendRing
{
    static_assert(Capacity > 0, "DeferredUdpSendRing capacity must be > 0");

    chip::FixedBuffer<Slot, Capacity> slots{};
    size_t head  = 0;
    size_t count = 0;

    bool Empty() const { return count == 0; }
    bool IsFull() const { return count == Capacity; }
    size_t ActiveCount() const { return count; }

    void PushBack(Slot && slot)
    {
        if (count == Capacity)
        {
            slots[head] = Slot{};
            head        = (head + 1) % Capacity;
            --count;
        }
        const size_t idx = (head + count) % Capacity;
        slots[idx]       = std::move(slot);
        ++count;
    }

    Slot PopFront()
    {
        Slot out = std::move(slots[head]);
        slots[head] = Slot{};
        head        = (head + 1) % Capacity;
        --count;
        return out;
    }
};

template <typename Slot, size_t Capacity>
void SwapDeferredUdpSendRings(DeferredUdpSendRing<Slot, Capacity> & a, DeferredUdpSendRing<Slot, Capacity> & b)
{
    std::swap(a.head, b.head);
    std::swap(a.count, b.count);
    for (size_t i = 0; i < Capacity; ++i)
    {
        std::swap(a.slots[i], b.slots[i]);
    }
}

} // namespace Inet
} // namespace chip
