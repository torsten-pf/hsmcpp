// Copyright (C) 2023 Igor Krechetov
// Distributed under MIT license. See file LICENSE for details
#ifndef HSMCPP_OS_STL_ATOMICFLAG_HPP
#define HSMCPP_OS_STL_ATOMICFLAG_HPP

#include <atomic>

#include "hsmcpp/os/UniqueLock.hpp"
#include "hsmcpp/os/ConditionVariable.hpp"
#include "hsmcpp/os/Mutex.hpp"

namespace hsmcpp {

class AtomicFlag {
public:
    AtomicFlag();
    ~AtomicFlag() = default;
    AtomicFlag(const AtomicFlag&) = delete;
    AtomicFlag& operator=(const AtomicFlag&) = delete;
    AtomicFlag& operator=(const AtomicFlag&) volatile = delete;

    bool test_and_set() noexcept;
    void clear() noexcept;
    bool test() const noexcept;
    UniqueLock lock() noexcept;
    void wait(const bool old) noexcept;
    void notify() noexcept;

private:
    Mutex mSync;
    ConditionVariable mWaitCond;
    std::atomic_bool mValue;
};

}  // namespace hsmcpp

#endif  // HSMCPP_OS_STL_ATOMICFLAG_HPP
