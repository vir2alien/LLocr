#pragma once

#include <QHash>
#include <QImage>
#include <QList>

namespace llocr {

class LruImageCache
{
public:
    explicit LruImageCache(qint64 budgetBytes) : m_budgetBytes(budgetBytes) {}

    const QImage *find(int key) const
    {
        const auto it = m_images.constFind(key);
        return it == m_images.constEnd() ? nullptr : &it.value();
    }

    void insert(int key, const QImage &image)
    {
        const auto existing = m_images.constFind(key);
        if (existing != m_images.constEnd())
            m_bytes -= existing->sizeInBytes();
        m_images.insert(key, image);
        m_bytes += image.sizeInBytes();
        m_order.removeAll(key);
        m_order.prepend(key);
        evict();
    }

    void touch(int key)
    {
        if (!m_images.contains(key))
            return;
        m_order.removeAll(key);
        m_order.prepend(key);
    }

    void clear()
    {
        m_images.clear();
        m_order.clear();
        m_bytes = 0;
    }

    void evict()
    {
        while (m_order.size() > 1 && m_bytes > m_budgetBytes) {
            const auto victim = m_images.find(m_order.takeLast());
            if (victim == m_images.end())
                continue;
            m_bytes -= victim->sizeInBytes();
            m_images.erase(victim);
        }
    }

private:
    QHash<int, QImage> m_images;
    QList<int> m_order;
    qint64 m_budgetBytes;
    qint64 m_bytes = 0;
};

}  // namespace llocr