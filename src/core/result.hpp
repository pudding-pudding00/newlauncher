#pragma once
// Fehlerbehandlung ohne Exceptions (Spec 4.2): fehlbare Funktionen liefern Result<T, E>.

#include "core/assert.hpp"

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace nl {

enum class ErrorCode : std::uint16_t {
    NotFound,
    IoError,
    InvalidData,
    VersionMismatch,
    NotMounted,
    Timeout,
    Unsupported,
};

struct Error {
    ErrorCode code;
    std::string context;
};

template <typename T, typename E = Error>
class [[nodiscard]] Result {
public:
    static Result ok(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    static Result err(E error) { return Result(std::in_place_index<1>, std::move(error)); }

    bool isOk() const { return data_.index() == 0; }

    T& value() & {
        NL_ASSERT(isOk());
        return *std::get_if<0>(&data_);
    }
    const T& value() const& {
        NL_ASSERT(isOk());
        return *std::get_if<0>(&data_);
    }
    T&& value() && {
        NL_ASSERT(isOk());
        return std::move(*std::get_if<0>(&data_));
    }

    const E& error() const {
        NL_ASSERT(!isOk());
        return *std::get_if<1>(&data_);
    }

    T valueOr(T fallback) const& { return isOk() ? *std::get_if<0>(&data_) : std::move(fallback); }

private:
    template <std::size_t I, typename U>
    Result(std::in_place_index_t<I> tag, U&& v) : data_(tag, std::forward<U>(v)) {}

    std::variant<T, E> data_;
};

template <typename E>
class [[nodiscard]] Result<void, E> {
public:
    static Result ok() { return Result(); }
    static Result err(E error) {
        Result r;
        r.error_ = std::move(error);
        r.ok_ = false;
        return r;
    }

    bool isOk() const { return ok_; }

    const E& error() const {
        NL_ASSERT(!ok_);
        return error_;
    }

private:
    Result() = default;

    bool ok_ = true;
    E error_{};
};

}  // namespace nl
