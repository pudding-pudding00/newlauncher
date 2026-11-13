#include "core/cheat_model.hpp"

#include <algorithm>

#include "core/cheat_select.hpp"

namespace nl {

namespace {
const std::string kEmpty;
}

CheatModel::CheatModel(Console console, std::string_view txt, std::string_view sel, bool wiiCheats) {
    if (console == Console::Wii && !wiiCheats) {
        status_ = CheatStatus::WiiUnavailable;
        return;
    }
    if (txt.empty()) return;
    auto parsed = parseGeckoTxt(txt);
    if (!parsed.isOk()) return;
    file_ = std::move(parsed).value();
    status_ = CheatStatus::Ready;

    const std::vector<std::string> names = parseSel(sel);
    rows_.reserve(file_.codes.size());
    for (std::size_t i = 0; i < file_.codes.size(); ++i) {
        const GeckoCode& c = file_.codes[i];
        bool first = true;
        for (std::size_t j = 0; j < i && first; ++j) first = file_.codes[j].name != c.name;
        const bool selectable = first && !c.hasPlaceholder;
        const bool checked = selectable && std::find(names.begin(), names.end(), c.name) != names.end();
        rows_.push_back(Row{i, selectable, checked});
    }
}

const std::string& CheatModel::label(std::size_t r) const { return r < rows_.size() ? file_.codes[rows_[r].code].name : kEmpty; }

const std::string& CheatModel::author(std::size_t r) const {
    return r < rows_.size() ? file_.codes[rows_[r].code].author : kEmpty;
}

bool CheatModel::checked(std::size_t r) const { return r < rows_.size() && rows_[r].checked; }

bool CheatModel::selectable(std::size_t r) const { return r < rows_.size() && rows_[r].selectable; }

bool CheatModel::toggle(std::size_t r) {
    if (!selectable(r)) return false;
    rows_[r].checked = !rows_[r].checked;
    dirty_ = true;
    return true;
}

std::vector<std::string> CheatModel::selection() const {
    std::vector<std::string> out;
    for (const Row& row : rows_)
        if (row.checked) out.push_back(file_.codes[row.code].name);
    return out;
}

std::string CheatModel::selText() const { return writeSel(selection()); }

}  // namespace nl
