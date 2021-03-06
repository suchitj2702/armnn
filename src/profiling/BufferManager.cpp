//
// Copyright © 2019 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "BufferManager.hpp"
#include "PacketBuffer.hpp"

namespace armnn
{

namespace profiling
{

BufferManager::BufferManager(unsigned int numberOfBuffers, unsigned int maxPacketSize)
    : m_MaxBufferSize(maxPacketSize),
      m_NumberOfBuffers(numberOfBuffers)
{
    Initialize();
}

IPacketBufferPtr BufferManager::Reserve(unsigned int requestedSize, unsigned int& reservedSize)
{
    reservedSize = 0;
    std::unique_lock<std::mutex> availableListLock(m_AvailableMutex, std::defer_lock);
    if (requestedSize > m_MaxBufferSize)
    {
        return nullptr;
    }
    availableListLock.lock();
    if (m_AvailableList.empty())
    {
        availableListLock.unlock();
        return nullptr;
    }
    IPacketBufferPtr buffer = std::move(m_AvailableList.back());
    m_AvailableList.pop_back();
    availableListLock.unlock();
    reservedSize = requestedSize;
    return buffer;
}

void BufferManager::Commit(IPacketBufferPtr& packetBuffer, unsigned int size, bool notifyConsumer)
{
    std::unique_lock<std::mutex> readableListLock(m_ReadableMutex, std::defer_lock);
    packetBuffer->Commit(size);
    readableListLock.lock();
    m_ReadableList.push_back(std::move(packetBuffer));
    readableListLock.unlock();

    if (notifyConsumer)
    {
        FlushReadList();
    }
}

void BufferManager::Initialize()
{
    m_AvailableList.reserve(m_NumberOfBuffers);
    for (unsigned int i = 0; i < m_NumberOfBuffers; ++i)
    {
        IPacketBufferPtr buffer = std::make_unique<PacketBuffer>(m_MaxBufferSize);
        m_AvailableList.emplace_back(std::move(buffer));
    }
    m_ReadableList.reserve(m_NumberOfBuffers);
}

void BufferManager::Release(IPacketBufferPtr& packetBuffer)
{
    std::unique_lock<std::mutex> availableListLock(m_AvailableMutex, std::defer_lock);
    packetBuffer->Release();
    availableListLock.lock();
    m_AvailableList.push_back(std::move(packetBuffer));
    availableListLock.unlock();
}

void BufferManager::Reset()
{
    //This method should only be called once all threads have been joined
    std::lock_guard<std::mutex> readableListLock(m_ReadableMutex);
    std::lock_guard<std::mutex> availableListLock(m_AvailableMutex);

    m_AvailableList.clear();
    m_ReadableList.clear();

    Initialize();
}

IPacketBufferPtr BufferManager::GetReadableBuffer()
{
    std::unique_lock<std::mutex> readableListLock(m_ReadableMutex);
    if (!m_ReadableList.empty())
    {
        IPacketBufferPtr buffer = std::move(m_ReadableList.back());
        m_ReadableList.pop_back();
        readableListLock.unlock();
        return buffer;
    }
    return nullptr;
}

void BufferManager::MarkRead(IPacketBufferPtr& packetBuffer)
{
    std::unique_lock<std::mutex> availableListLock(m_AvailableMutex, std::defer_lock);
    packetBuffer->MarkRead();
    availableListLock.lock();
    m_AvailableList.push_back(std::move(packetBuffer));
    availableListLock.unlock();
}

void BufferManager::SetConsumer(IConsumer* consumer)
{
    m_Consumer = consumer;
}

void BufferManager::FlushReadList()
{
    // notify consumer that packet is ready to read
    if (m_Consumer != nullptr)
    {
        m_Consumer->SetReadyToRead();
    }
}

} // namespace profiling

} // namespace armnn
