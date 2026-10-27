// STYM2149 — pluginui: long job outside the message thread (exports of the File menu)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The prototype ran its exports in the page, which stayed frozen under the bee (busyWrap); here, the offline render
// runs on a thread of its own (PLAN_M7) and the editor stays alive. One job at a time: `work` runs on the
// thread, without touching the editor (it works on its copies), and reads `cancel` to stop early; poll, called by the
// message thread at each editor tick, calls `done` on that thread once `work` has finished. Destruction (editor
// closed) asks for the stop and waits for the thread, without calling `done`.
#pragma once
#include <atomic>
#include <functional>
#include <thread>

namespace stym::pluginui {

// Owned by MainWindow (MainWindow::startJob); start, poll and the destructor are called on the message thread only.
// The worker thread touches nothing but what `work` captured and the two atomics.
class BackgroundJob {
public:
    // Body of the job, run on the worker thread; it should return soon after `cancel` turns true.
    using Work = std::function<void(const std::atomic<bool>& cancel)>;

    BackgroundJob() = default;
    ~BackgroundJob();
    BackgroundJob(const BackgroundJob&) = delete;
    BackgroundJob& operator=(const BackgroundJob&) = delete;

    // False (nothing is started) if a job is still running.
    bool start(Work work, std::function<void()> done);
    // From start until poll has collected the finished job (the thread is joinable until then).
    bool running() const noexcept { return thread_.joinable(); }
    // Message thread: true if the job has just finished (`done` called).
    bool poll();

private:
    std::thread thread_;
    // cancel_: message thread → worker (stop early); finished_: worker → message thread (`work` has returned).
    std::atomic<bool> cancel_{false}, finished_{false};
    std::function<void()> done_;  // touched by the message thread only
};

}  // namespace stym::pluginui
