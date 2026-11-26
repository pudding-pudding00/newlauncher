#pragma once
// Abgleich-Zustand <data>/cloud/state.ini (M27): je "<anbieter>:<konto>/<einheit>" die Basis (hash, rev) und die
// Unix-Zeit der letzten Sitzung (für den Konfliktdialog). Keine Tokens, keine persönlichen Daten.

#include <optional>
#include <string>
#include <string_view>

#include "core/cloud_sync.hpp"
#include "core/ini.hpp"
#include "core/types.hpp"

namespace nl {

class CloudState {
public:
    static CloudState parse(std::string_view text);
    std::string serialize() const;

    std::optional<SyncBase> base(std::string_view acct, std::string_view unit) const;
    void setBase(std::string_view acct, std::string_view unit, const SyncBase& b);
    u64 played(std::string_view acct, std::string_view unit) const;
    void setPlayed(std::string_view acct, std::string_view unit, u64 unixTime);
    void clearAccount(std::string_view acct);  // Abmelden

private:
    IniDoc ini_;
};

}  // namespace nl
