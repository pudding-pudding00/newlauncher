#include "core/imd5.hpp"

#include "core/lz77.hpp"

namespace nl {

namespace {
constexpr std::size_t kImd5Header = 32;
Result<std::vector<u8>> invalid(const char* what) {
    return Result<std::vector<u8>>::err(Error{ErrorCode::InvalidData, what});
}
}  // namespace

Result<std::vector<u8>> unwrapImd5(std::span<const u8> data, u32 maxOut) {
    if (data.size() < kImd5Header || data[0] != 'I' || data[1] != 'M' || data[2] != 'D' || data[3] != '5')
        return invalid("IMD5: kein Kopf");
    const u32 size = static_cast<u32>(data[4]) << 24 | static_cast<u32>(data[5]) << 16 | static_cast<u32>(data[6]) << 8 | data[7];
    if (size > data.size() - kImd5Header) return invalid("IMD5: Größe hinter dem Ende");
    const std::span<const u8> payload = data.subspan(kImd5Header, size);
    if (isLz77(payload)) return lz77Decompress(payload, maxOut);
    if (size > maxOut) return invalid("IMD5: zu groß");
    return Result<std::vector<u8>>::ok(std::vector<u8>(payload.begin(), payload.end()));
}

}  // namespace nl
