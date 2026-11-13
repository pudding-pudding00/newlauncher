#pragma once
// Filterzeile (M17, Spec v2 10.3): Alle, Favoriten, Genres, eigene Kategorien. Die A–Z-Leiste und das
// Such-Overlay blättern sie über diese Schnittstelle; MainScene liefert die Filter des aktuellen Eintrags.

#include <string>

namespace nl::ui {

class FilterSource {
public:
    virtual ~FilterSource() = default;
    // 0 Filter = keine Zeile. setFilterIndex wirkt sofort auf die Liste.
    virtual int filterCount() const { return 0; }
    virtual int filterIndex() const { return 0; }
    virtual std::string filterLabel(int) const { return {}; }
    virtual void setFilterIndex(int) {}
};

// Index des nächsten Filters in Richtung dir (±1) mit Umlauf; n > 0.
inline int stepFilterIndex(int index, int dir, int n) { return ((index + dir) % n + n) % n; }

}  // namespace nl::ui
