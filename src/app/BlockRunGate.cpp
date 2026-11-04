#include "app/BlockRunGate.h"

#include "app/BlockQueue.h"

namespace llocr {

void BlockRunGate::setExternalBusy(std::function<bool()> busy)
{
    m_externalBusy = std::move(busy);
}

bool BlockRunGate::tryAcquire(BlockQueue *owner)
{
    if (m_holder || (m_externalBusy && m_externalBusy()))
        return false;
    m_holder = owner;
    m_lastHolder = owner;
    return true;
}

void BlockRunGate::release(BlockQueue *owner)
{
    if (m_holder == owner)
        m_holder.clear();
}

bool BlockRunGate::held() const
{
    return !m_holder.isNull();
}

BlockQueue *BlockRunGate::holder() const
{
    return m_holder.data();
}

BlockQueue *BlockRunGate::lastHolder() const
{
    return m_lastHolder.data();
}

}  // namespace llocr
