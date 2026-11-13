// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"

namespace {

struct Shared {
    base::Lock lock;
    base::ConditionVariable changed{&lock};
    base::WaitableEvent finished{base::WaitableEvent::ResetPolicy::AUTOMATIC};
    unsigned permits = 0;
    unsigned done = 0;
    bool release_all = false;
};

class Worker final : public base::PlatformThread::Delegate {
public:
    explicit Worker(Shared& shared) : shared_(shared) {}

    void ThreadMain() override {
        base::AutoLock hold(shared_.lock);
        ready.Signal();
        while (!shared_.release_all && shared_.permits == 0)
            shared_.changed.Wait();
        if (!shared_.release_all)
            --shared_.permits;
        ++shared_.done;
        shared_.finished.Signal();
    }

    base::WaitableEvent ready{base::WaitableEvent::ResetPolicy::AUTOMATIC};

private:
    Shared& shared_;
};

}  // namespace

int TestContentConditionVariable() {
    Shared shared;
    Worker first(shared), second(shared);
    base::PlatformThreadHandle first_thread, second_thread;
    if (!base::PlatformThread::Create(0, &first, &first_thread))
        return 151;
    const bool started_second = base::PlatformThread::Create(0, &second, &second_thread);
    const bool waiting = started_second && first.ready.TimedWait(base::Seconds(2)) &&
        second.ready.TimedWait(base::Seconds(2));
    bool one_awoke = false;
    if (waiting) {
        {
            base::AutoLock hold(shared.lock);
            shared.permits = 1;
            shared.changed.Signal();
        }
        one_awoke = shared.finished.TimedWait(base::Seconds(2));
    }
    unsigned done_after_signal;
    {
        base::AutoLock hold(shared.lock);
        done_after_signal = shared.done;
        shared.release_all = true;
        shared.changed.Broadcast();
    }
    base::PlatformThread::Join(first_thread);
    if (started_second)
        base::PlatformThread::Join(second_thread);
    if (!waiting || !one_awoke || done_after_signal != 1 ||
        shared.done != 2 || shared.permits != 0)
        return 152;

    const auto duration = base::Milliseconds(30);
    base::AutoLock hold(shared.lock);
    const auto start = base::TimeTicks::Now();
    shared.changed.TimedWait(duration);
    shared.lock.AssertAcquired();
    const auto elapsed = base::TimeTicks::Now() - start;
    if (elapsed < duration || elapsed >= base::Seconds(2))
        return 153;

    shared.changed.TimedWait(base::TimeDelta());
    shared.lock.AssertAcquired();
    return shared.done == 2 ? 0 : 154;
}
