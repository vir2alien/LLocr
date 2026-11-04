#include "app/BlockRunGate.h"

#include "app/BlockQueue.h"

namespace llocr {

void BlockRunGate::setExternalBusy(std::function<bool()> busy)
{
    m_externalBusy = std::move(busy);
}

bool BlockRunGate::tryAcquire(BlockQueue *owner)
{
    if (!owner || m_holder || (m_externalBusy && m_externalBusy()))
        return false;
    m_holder = owner;
    return true;
}

void BlockRunGate::release(BlockQueue *owner)
{
    if (m_holder == owner)
        m_holder.clear();
}

}  // namespace llocr
