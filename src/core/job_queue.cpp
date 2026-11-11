#include "core/job_queue.hpp"

#include <algorithm>
#include <utility>

namespace nl {

u32 JobQueue::push(NetJobFn run) {
    if (nextId_ == 0) nextId_ = 1;  // nach 2^32 Aufträgen; praktisch nie
    const u32 id = nextId_++;
    queue_.push_back(NetJob{id, std::move(run)});
    return id;
}

std::optional<NetJob> JobQueue::take() {
    if (queue_.empty()) return std::nullopt;
    NetJob job = std::move(queue_.front());
    queue_.pop_front();
    current_ = job.id;
    cancel_ = false;
    return job;
}

void JobQueue::finish() {
    current_ = 0;
    cancel_ = false;
}

void JobQueue::cancel(u32 id) {
    if (id == 0) return;
    if (id == current_) {
        cancel_ = true;
        return;
    }
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [id](const NetJob& j) { return j.id == id; }),
                 queue_.end());
}

void JobQueue::cancelAll() {
    queue_.clear();
    if (current_ != 0) cancel_ = true;
}

bool JobQueue::cancelled(u32 id) const { return id != 0 && id == current_ && cancel_; }

bool JobQueue::active(u32 id) const {
    if (id == 0) return false;
    if (id == current_) return true;
    return std::any_of(queue_.begin(), queue_.end(), [id](const NetJob& j) { return j.id == id; });
}

}  // namespace nl
