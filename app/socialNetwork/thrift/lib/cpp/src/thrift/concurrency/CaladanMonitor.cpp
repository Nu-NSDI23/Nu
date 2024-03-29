/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <thrift/thrift-config.h>

#include <thrift/concurrency/Monitor.h>
#include <thrift/concurrency/Exception.h>
#include <thrift/concurrency/Util.h>
#include <assert.h>
#include <climits>

#include <chrono>

#include <thread.h>
#include <sync.h>

namespace apache {
namespace thrift {
namespace concurrency {

/**
 * Monitor implementation using the std thread library
 *
 * @version $Id:$
 */
class Monitor::Impl {

public:
  Impl() : ownedMutex_(new Mutex()), conditionVariable_(), mutex_(NULL) {
    init(ownedMutex_.get());
  }

  Impl(Mutex* mutex) : ownedMutex_(), conditionVariable_(), mutex_(NULL) { init(mutex); }

  Impl(Monitor* monitor) : ownedMutex_(), conditionVariable_(), mutex_(NULL) {
    init(&(monitor->mutex()));
  }

  Mutex& mutex() { return *mutex_; }
  void lock() { mutex_->lock(); }
  void unlock() { mutex_->unlock(); }

  /**
   * Exception-throwing version of waitForTimeRelative(), called simply
   * wait(int64) for historical reasons.  Timeout is in milliseconds.
   *
   * If the condition occurs,  this function returns cleanly; on timeout or
   * error an exception is thrown.
   */
  void wait(int64_t timeout_ms) {
    int result = waitForTimeRelative(timeout_ms);
    if (result == THRIFT_ETIMEDOUT) {
      throw TimedOutException();
    } else if (result != 0) {
      throw TException("Monitor::wait() failed");
    }
  }

  /**
   * Waits until the specified timeout in milliseconds for the condition to
   * occur, or waits forever if timeout_ms == 0.
   *
   * Returns 0 if condition occurs, THRIFT_ETIMEDOUT on timeout, or an error code.
   */
  int waitForTimeRelative(int64_t timeout_ms) {
    if (timeout_ms == 0LL) {
      return waitForever();
    }

    assert(mutex_);
    rt::TimedMutex* mutexImpl = static_cast<rt::TimedMutex*>(mutex_->getUnderlyingImpl());
    assert(mutexImpl);

    bool timedout = !conditionVariable_.WaitFor(mutexImpl, timeout_ms * 1000);
    mutexImpl->Unlock();
    return (timedout ? THRIFT_ETIMEDOUT : 0);
  }

  /**
   * Waits until the absolute time specified using struct THRIFT_TIMESPEC.
   * Returns 0 if condition occurs, THRIFT_ETIMEDOUT on timeout, or an error code.
   */
  int waitForTime(const THRIFT_TIMESPEC* abstime) {
    struct timeval temp;
    temp.tv_sec = static_cast<long>(abstime->tv_sec);
    temp.tv_usec = static_cast<long>(abstime->tv_nsec) / 1000;
    return waitForTime(&temp);
  }

  /**
   * Waits until the absolute time specified using struct timeval.
   * Returns 0 if condition occurs, THRIFT_ETIMEDOUT on timeout, or an error code.
   */
  int waitForTime(const struct timeval* abstime) {
    assert(mutex_);
    rt::TimedMutex* mutexImpl = static_cast<rt::TimedMutex*>(mutex_->getUnderlyingImpl());
    assert(mutexImpl);

    struct timeval currenttime;
    Util::toTimeval(currenttime, Util::currentTime());

    long tv_sec = static_cast<long>(abstime->tv_sec - currenttime.tv_sec);
    long tv_usec = static_cast<long>(abstime->tv_usec - currenttime.tv_usec);
    if (tv_sec < 0)
      tv_sec = 0;
    if (tv_usec < 0)
      tv_usec = 0;

    bool timedout = !conditionVariable_.WaitFor(mutexImpl, tv_sec * 1000 * 1000 + tv_usec);
    mutexImpl->Unlock();
    return (timedout ? THRIFT_ETIMEDOUT : 0);
  }

  /**
   * Waits forever until the condition occurs.
   * Returns 0 if condition occurs, or an error code otherwise.
   */
  int waitForever() {
    assert(mutex_);
    rt::TimedMutex* mutexImpl = static_cast<rt::TimedMutex*>(mutex_->getUnderlyingImpl());
    assert(mutexImpl);

    conditionVariable_.WaitUntil(mutexImpl, std::numeric_limits<uint64_t>::max());
    mutexImpl->Unlock();
    return 0;
  }

  void notify() { conditionVariable_.Signal(); }

  void notifyAll() { conditionVariable_.SignalAll(); }

private:
  void init(Mutex* mutex) { mutex_ = mutex; }

  const std::unique_ptr<Mutex> ownedMutex_;
  rt::TimedCondVar conditionVariable_;
  Mutex* mutex_;
};

Monitor::Monitor() : impl_(new Monitor::Impl()) {
}
Monitor::Monitor(Mutex* mutex) : impl_(new Monitor::Impl(mutex)) {
}
Monitor::Monitor(Monitor* monitor) : impl_(new Monitor::Impl(monitor)) {
}

Monitor::~Monitor() {
  delete impl_;
}

Mutex& Monitor::mutex() const {
  return const_cast<Monitor::Impl*>(impl_)->mutex();
}

void Monitor::lock() const {
  const_cast<Monitor::Impl*>(impl_)->lock();
}

void Monitor::unlock() const {
  const_cast<Monitor::Impl*>(impl_)->unlock();
}

void Monitor::wait(int64_t timeout) const {
  const_cast<Monitor::Impl*>(impl_)->wait(timeout);
}

int Monitor::waitForTime(const THRIFT_TIMESPEC* abstime) const {
  return const_cast<Monitor::Impl*>(impl_)->waitForTime(abstime);
}

int Monitor::waitForTime(const timeval* abstime) const {
  return const_cast<Monitor::Impl*>(impl_)->waitForTime(abstime);
}

int Monitor::waitForTimeRelative(int64_t timeout_ms) const {
  return const_cast<Monitor::Impl*>(impl_)->waitForTimeRelative(timeout_ms);
}

int Monitor::waitForever() const {
  return const_cast<Monitor::Impl*>(impl_)->waitForever();
}

void Monitor::notify() const {
  const_cast<Monitor::Impl*>(impl_)->notify();
}

void Monitor::notifyAll() const {
  const_cast<Monitor::Impl*>(impl_)->notifyAll();
}
}
}
} // apache::thrift::concurrency
