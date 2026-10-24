// STYM2149 — pluginui: long job off the message thread (exports of the File menu, M7, D-170)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/BackgroundJob.h"

#include <utility>

namespace stym::pluginui {

BackgroundJob::~BackgroundJob() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

bool BackgroundJob::start(Work work, std::function<void()> done) {
    if (running()) return false;
    cancel_ = false;
    finished_ = false;
    done_ = std::move(done);
    // The flag is raised once `work` has returned (release): when poll sees it (acquire), its join only waits for the
    // thread's exit and never blocks the message thread during the job.
    thread_ = std::thread([this, work = std::move(work)] {
        work(cancel_);
        finished_.store(true, std::memory_order_release);
    });
    return true;
}

bool BackgroundJob::poll() {
    if (!running() || !finished_.load(std::memory_order_acquire)) return false;
    thread_.join();
    // `done` leaves the member before it runs: the job is over (running() false), so it may start the next job,
    // which stores its own `done`.
    auto done = std::move(done_);
    done_ = nullptr;
    if (done) done();
    return true;
}

}  // namespace stym::pluginui
