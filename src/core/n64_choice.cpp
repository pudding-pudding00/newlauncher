#include "core/n64_choice.hpp"

#include <algorithm>

namespace nl {

namespace {
constexpr std::size_t idx(N64Method m) { return static_cast<std::size_t>(m); }
}  // namespace

N64Decision decideN64(const N64Inputs& in) {
    N64Decision d;
    if (!in.mappedTitle) d.why[idx(N64Method::Vc)] = N64Unavailable::NotMapped;
    else if (std::find(in.installedTitles.begin(), in.installedTitles.end(), *in.mappedTitle) == in.installedTitles.end())
        d.why[idx(N64Method::Vc)] = N64Unavailable::NotInstalled;
    if (!in.not64Dol) d.why[idx(N64Method::Not64)] = N64Unavailable::DolMissing;
    if (!in.wii64Dol) d.why[idx(N64Method::Wii64)] = N64Unavailable::DolMissing;
    d.mappingIgnored = d.why[idx(N64Method::Vc)] == N64Unavailable::NotInstalled;

    if (in.override_ && d.available(*in.override_)) {
        d.chosen = in.override_;
        return d;
    }
    d.overrideIgnored = in.override_.has_value();
    for (N64Method m : {N64Method::Vc, N64Method::Not64, N64Method::Wii64})
        if (d.available(m)) {
            d.chosen = m;
            break;
        }
    return d;
}

}  // namespace nl
