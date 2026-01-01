#pragma once
// Result.hpp - Error handling without exceptions
// Because try-catch blocks in hot paths are for people who hate performance

#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace vc {

// Error type with context
struct Error {
    std::string message;
    std::string location;
    int code{0};

    Error() = default;

    explicit Error(std::string_view msg,
                   std::source_location loc = std::source_location::current())
        : message(msg),
          location(std::string(loc.file_name()) + ":" +
                   std::to_string(loc.line())),
          code(-1) {
    }

    Error(std::string_view msg,
          int err_code,
          std::source_location loc = std::source_location::current())
        : message(msg),
          location(std::string(loc.file_name()) + ":" +
                   std::to_string(loc.line())),
          code(err_code) {
    }

    std::string full() const {
        return message + " [" + location + "]" +
               (code ? " (code: " + std::to_string(code) + ")" : "");
    }
};

// Result type - either value or error
template <typename T>
class Result {
    std::variant<T, Error> data_;

public:
    // Constructors
    Result(T value) : data_(std::move(value)) {
    }
    Result(Error error) : data_(std::move(error)) {
    }

    // Static factories
    static Result ok(T value) {
        return Result(std::move(value));
    }
    static Result err(std::string_view msg) {
        return Result(Error(msg));
    }
    static Result err(Error e) {
        return Result(std::move(e));
    }

    // Checks
    [[nodiscard]] bool isOk() const {
        return std::holds_alternative<T>(data_);
    }
    [[nodiscard]] bool isErr() const {
        return std::holds_alternative<Error>(data_);
    }
    [[nodiscard]] explicit operator bool() const {
        return isOk();
    }

    // Accessors (check first or face UB, junior)
    [[nodiscard]] T& value() & {
        return std::get<T>(data_);
    }
    [[nodiscard]] const T& value() const& {
        return std::get<T>(data_);
    }
    [[nodiscard]] T&& value() && {
        return std::get<T>(std::move(data_));
    }

    [[nodiscard]] Error& error() & {
        return std::get<Error>(data_);
    }
    [[nodiscard]] const Error& error() const& {
        return std::get<Error>(data_);
    }

    // Safe access with default
    [[nodiscard]] T valueOr(T defaultVal) const {
        return isOk() ? std::get<T>(data_) : std::move(defaultVal);
    }

    // Monadic operations (functional programming gang)
    template <typename F>
    auto map(F&& fn) -> Result<decltype(fn(std::declval<T>()))> {
        using U = decltype(fn(std::declval<T>()));
        if (isOk()) {
            return Result<U>::ok(fn(value()));
        }
        return Result<U>::err(error());
    }

    template <typename F>
    auto andThen(F&& fn) -> decltype(fn(std::declval<T>())) {
        if (isOk()) {
            return fn(value());
        }
        return decltype(fn(std::declval<T>()))::err(error());
    }

    // Pointer-like access
    T* operator->() {
        return &value();
    }
    const T* operator->() const {
        return &value();
    }
    T& operator*() & {
        return value();
    }
    const T& operator*() const& {
        return value();
    }
};

// Specialization for void
template <>
class Result<void> {
    std::optional<Error> error_;

public:
    Result() = default;
    Result(Error e) : error_(std::move(e)) {
    }

    static Result ok() {
        return Result();
    }
    static Result err(std::string_view msg) {
        return Result(Error(msg));
    }
    static Result err(Error e) {
        return Result(std::move(e));
    }

    [[nodiscard]] bool isOk() const {
        return !error_.has_value();
    }
    [[nodiscard]] bool isErr() const {
        return error_.has_value();
    }
    [[nodiscard]] explicit operator bool() const {
        return isOk();
    }

    [[nodiscard]] const Error& error() const {
        return *error_;
    }
};

// Helper macro for early return on error (the ? operator we wish we had)
#define TRY(expr)                                              \
    do {                                                       \
        auto _result = (expr);                                 \
        if (!_result)                                          \
            return Result<decltype(_result)::value_type>::err( \
                    _result.error());                          \
    } while (0)

#define TRY_VOID(expr)                                 \
    do {                                               \
        auto _result = (expr);                         \
        if (!_result)                                  \
            return Result<void>::err(_result.error()); \
    } while (0)

} // namespace vc