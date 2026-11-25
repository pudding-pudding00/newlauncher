#pragma once
// Cover-LRU (Spec 4.4, 6.4): Byte-Budget, Anfragen nach Priorität, Einblendung über 150 ms.
// Ein im aktuellen Frame gewünschtes Cover wird nie verdrängt; das Budget kann um diese überschritten werden.

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/cmpr.hpp"
#include "core/types.hpp"

namespace nl {

struct CoverMemory {
    virtual void* alloc(u32 bytes) = 0;  // Wii: memalign(32)
    virtual void free(void* p) = 0;

protected:
    ~CoverMemory() = default;
};

enum class CoverState : u8 { Unknown, Pending, Ready, Missing };
enum class CoverPriority : u8 { Prefetch, Visible };

inline constexpr u32 kCoverBudgetBytes = 16u * 1024 * 1024;
inline constexpr float kCoverFadeMs = 150.0f;

class CoverCache {
public:
    CoverCache(u32 budgetBytes, CoverMemory& mem);
    ~CoverCache();
    CoverCache(const CoverCache&) = delete;
    CoverCache& operator=(const CoverCache&) = delete;

    void beginFrame(u32 frame, float nowMs);
    // Allokiert nur beim ersten Wunsch eines Stamms.
    CoverState want(std::string_view stem, CoverPriority priority);
    // Gewünschter Pending-Eintrag mit höchster Priorität (bei Gleichstand der zuletzt gewünschte);
    // er gilt danach als in Arbeit. Verdrängt vorher auf das Budget.
    std::optional<std::string> nextRequest();
    // texels aus mem.alloc; box: Inhalt ohne transparenten Rand (M23), nullopt = ganze Textur
    void complete(std::string_view stem, void* texels, u32 bytes, std::optional<PixelBox> box = std::nullopt);
    void fail(std::string_view stem);
    void invalidate(std::string_view stem);  // Missing → Unknown (nach einem Download)
    void clear();

    CoverState state(std::string_view stem) const;
    const void* texels(std::string_view stem) const;
    float fadeAlpha(std::string_view stem) const;
    std::optional<PixelBox> contentBox(std::string_view stem) const;  // nur Ready mit Angabe
    std::optional<u16> slotOf(std::string_view stem) const;  // nur Ready
    // Perf (M7): Zeitpunkt des ersten Visible-want in der laufenden Pending-Phase; Prefetch zählt nicht,
    // weil die Latenz messen soll, wie lange ein sichtbarer Platz leer bleibt.
    std::optional<float> requestedAtMs(std::string_view stem) const;
    float nowMs() const { return nowMs_; }  // Zeit aus dem letzten beginFrame
    const void* texelsOfSlot(u16 slot) const;
    u32 bytesUsed() const { return bytesUsed_; }
    u32 readyCount() const { return readyCount_; }
    bool idle() const;  // nichts Pending, nichts in Arbeit
    u32 generation() const { return generation_; }  // ändert sich, wenn Texel kommen oder gehen

private:
    struct Entry {
        std::string stem;
        CoverState state = CoverState::Unknown;
        CoverPriority priority = CoverPriority::Prefetch;
        bool inFlight = false;
        u32 lastWanted = 0;
        u32 wantSeq = 0;
        void* texels = nullptr;
        u32 bytes = 0;
        float readyAtMs = 0;
        std::optional<PixelBox> box{};
        float requestedAtMs = -1;  // < 0: keine Visible-Anfrage in dieser Pending-Phase
    };
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    Entry* find(std::string_view stem);
    const Entry* find(std::string_view stem) const;
    void release(Entry& e);
    void evict();

    u32 budget_;
    CoverMemory& mem_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, u16, Hash, std::equal_to<>> index_;
    u32 frame_ = 0;
    float nowMs_ = 0;
    u32 seq_ = 0;
    u32 bytesUsed_ = 0;
    u32 readyCount_ = 0;
    u32 generation_ = 0;
};

}  // namespace nl
