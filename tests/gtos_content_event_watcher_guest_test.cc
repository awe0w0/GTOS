// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/waitable_event.h"
#include "base/synchronization/waitable_event_watcher.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"

namespace {

using Event = base::WaitableEvent;
using Watcher = base::WaitableEventWatcher;

// Hold posted callbacks until the test explicitly pumps the origin sequence.
class Queue final : public base::SequencedTaskRunner {
public:
    bool PostDelayedTask(const base::Location&, base::OnceClosure task,
                         base::TimeDelta delay) override {
        if (!delay.is_zero()) return false;
        tasks_.push_back(std::move(task));
        return true;
    }

    bool PostNonNestableDelayedTask(const base::Location& from, base::OnceClosure task,
                                    base::TimeDelta delay) override {
        return PostDelayedTask(from, std::move(task), delay);
    }

    bool RunsTasksInCurrentSequence() const override { return true; }

    void Drain() {
        while (!tasks_.empty()) {
            auto task = std::move(tasks_.front());
            tasks_.pop_front();
            std::move(task).Run();
        }
    }

private:
    ~Queue() override = default;

    std::deque<base::OnceClosure> tasks_;
};

bool Watch(Watcher& watcher, Event& event, unsigned& calls,
           const scoped_refptr<Queue>& queue) {
    return watcher.StartWatching(
        &event, base::BindOnce([](unsigned* count, Event* expected, Event* fired) {
            if (expected == fired) ++*count;
        }, base::Unretained(&calls), base::Unretained(&event)), queue);
}

}  // namespace

int TestContentEventWatcher() {
    const auto queue = base::MakeRefCounted<Queue>();
    Event manual(Event::ResetPolicy::MANUAL, Event::InitialState::NOT_SIGNALED);
    Watcher first, second;
    unsigned calls = 0;
    if (!Watch(first, manual, calls, queue) || !Watch(second, manual, calls, queue))
        return 134;
    manual.Signal();
    manual.Reset();
    if (calls != 0 || manual.IsSignaled()) return 135;
    queue->Drain();
    if (calls != 2) return 136;

    manual.Signal();
    if (!Watch(first, manual, calls, queue)) return 137;
    first.StopWatching();
    queue->Drain();
    if (calls != 2 || !manual.IsSignaled()) return 138;

    manual.Reset();
    auto dropped = std::make_unique<Watcher>();
    if (!Watch(*dropped, manual, calls, queue)) return 155;
    manual.Signal();
    dropped.reset();
    queue->Drain();
    if (calls != 2 || !manual.IsSignaled()) return 156;

    Event automatic(Event::ResetPolicy::AUTOMATIC, Event::InitialState::NOT_SIGNALED);
    if (!Watch(first, automatic, calls, queue) || !Watch(second, automatic, calls, queue))
        return 139;
    automatic.Signal();
    queue->Drain();
    if (calls != 3 || automatic.IsSignaled()) return 140;
    automatic.Signal();
    queue->Drain();
    if (calls != 4 || automatic.IsSignaled()) return 141;

    automatic.Signal();
    if (!Watch(first, automatic, calls, queue) || automatic.IsSignaled()) return 142;
    queue->Drain();
    if (calls != 5) return 143;

    bool rearmed = false;
    if (!first.StartWatching(
            &manual,
            base::BindOnce([](Watcher* watcher, Queue* runner, unsigned* count,
                              bool* started, Event* event) {
                ++*count;
                *started = Watch(*watcher, *event, *count, base::WrapRefCounted(runner));
            }, base::Unretained(&first), base::Unretained(queue.get()),
               base::Unretained(&calls), base::Unretained(&rearmed)),
            queue)) return 144;
    queue->Drain();
    if (!rearmed || calls != 7) return 145;

    auto deleted = std::make_unique<Event>(Event::ResetPolicy::MANUAL,
                                           Event::InitialState::NOT_SIGNALED);
    if (!first.StartWatching(
            deleted.get(), base::BindOnce([](unsigned* count, Event*) { ++*count; },
                                          base::Unretained(&calls)), queue))
        return 146;
    deleted->Signal();
    deleted.reset();
    queue->Drain();
    if (calls != 8) return 147;

    for (unsigned i = 0; i < 64; ++i) {
        auto pending = std::make_unique<Event>(Event::ResetPolicy::AUTOMATIC,
                                               Event::InitialState::NOT_SIGNALED);
        if (!Watch(first, *pending, calls, queue)) return 148;
        pending.reset();
        first.StopWatching();
    }
    if (!Watch(first, automatic, calls, queue)) return 149;
    first.StopWatching();
    automatic.Signal();
    queue->Drain();
    return calls == 8 && automatic.IsSignaled() ? 0 : 150;
}
