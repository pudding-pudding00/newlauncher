#include "core/cover_cache.hpp"

#include <algorithm>

namespace nl {

CoverCache::CoverCache(u32 budgetBytes, CoverMemory& mem) : budget_(budgetBytes), mem_(mem) {}

CoverCache::~CoverCache() { clear(); }

CoverCache::Entry* CoverCache::find(std::string_view stem) {
    const auto it = index_.find(stem);
    return it == index_.end() ? nullptr : &entries_[it->second];
}

const CoverCache::Entry* CoverCache::find(std::string_view stem) const {
    const auto it = index_.find(stem);
    return it == index_.end() ? nullptr : &entries_[it->second];
}

void CoverCache::beginFrame(u32 frame, float nowMs) {
    frame_ = frame;
    nowMs_ = nowMs;
    // Veraltete Anfragen verfallen, damit schnelles Scrollen keine Warteschlange aufstaut.
    for (auto& e : entries_)
        if (e.state == CoverState::Pending && !e.inFlight && e.lastWanted + 1 < frame) e.state = CoverState::Unknown;
}

CoverState CoverCache::want(std::string_view stem, CoverPriority priority) {
    Entry* e = find(stem);
    if (!e) {
        index_.emplace(std::string(stem), u16(entries_.size()));
        entries_.push_back(Entry{std::string(stem)});
        e = &entries_.back();
    }
    if (e->lastWanted != frame_ || priority > e->priority) e->priority = priority;
    e->lastWanted = frame_;
    e->wantSeq = ++seq_;
    if (e->state == CoverState::Unknown) e->state = CoverState::Pending;
    return e->state;
}

std::optional<std::string> CoverCache::nextRequest() {
    evict();
    Entry* best = nullptr;
    for (auto& e : entries_) {
        if (e.state != CoverState::Pending || e.inFlight || e.lastWanted != frame_) continue;
        if (!best || e.priority > best->priority || (e.priority == best->priority && e.wantSeq > best->wantSeq))
            best = &e;
    }
    if (!best) return std::nullopt;
    best->inFlight = true;
    return best->stem;
}

void CoverCache::complete(std::string_view stem, void* texels, u32 bytes) {
    Entry* e = find(stem);
    if (!e) {
        mem_.free(texels);
        return;
    }
    release(*e);
    e->inFlight = false;
    e->state = CoverState::Ready;
    e->texels = texels;
    e->bytes = bytes;
    e->readyAtMs = nowMs_;
    bytesUsed_ += bytes;
    ++readyCount_;
    ++generation_;
    evict();
}

void CoverCache::fail(std::string_view stem) {
    if (Entry* e = find(stem)) {
        release(*e);
        e->inFlight = false;
        e->state = CoverState::Missing;
    }
}

void CoverCache::invalidate(std::string_view stem) {
    if (Entry* e = find(stem); e && e->state == CoverState::Missing) e->state = CoverState::Unknown;
}

void CoverCache::release(Entry& e) {
    if (!e.texels) return;
    mem_.free(e.texels);
    e.texels = nullptr;
    bytesUsed_ -= e.bytes;
    e.bytes = 0;
    --readyCount_;
    ++generation_;
}

void CoverCache::evict() {
    while (bytesUsed_ > budget_) {
        Entry* oldest = nullptr;
        for (auto& e : entries_)
            if (e.state == CoverState::Ready && e.lastWanted != frame_ &&
                (!oldest || e.lastWanted < oldest->lastWanted))
                oldest = &e;
        if (!oldest) return;
        release(*oldest);
        oldest->state = CoverState::Unknown;
    }
}

void CoverCache::clear() {
    for (auto& e : entries_) release(e);
    entries_.clear();
    index_.clear();
    ++generation_;
}

CoverState CoverCache::state(std::string_view stem) const {
    const Entry* e = find(stem);
    return e ? e->state : CoverState::Unknown;
}

const void* CoverCache::texels(std::string_view stem) const {
    const Entry* e = find(stem);
    return e ? e->texels : nullptr;
}

float CoverCache::fadeAlpha(std::string_view stem) const {
    const Entry* e = find(stem);
    if (!e || e->state != CoverState::Ready) return 0.0f;
    return std::clamp((nowMs_ - e->readyAtMs) / kCoverFadeMs, 0.0f, 1.0f);
}

std::optional<u16> CoverCache::slotOf(std::string_view stem) const {
    const auto it = index_.find(stem);
    if (it == index_.end() || entries_[it->second].state != CoverState::Ready) return std::nullopt;
    return it->second;
}

const void* CoverCache::texelsOfSlot(u16 slot) const {
    return slot < entries_.size() ? entries_[slot].texels : nullptr;
}

bool CoverCache::idle() const {
    return std::none_of(entries_.begin(), entries_.end(),
                        [](const Entry& e) { return e.inFlight || e.state == CoverState::Pending; });
}

}  // namespace nl
