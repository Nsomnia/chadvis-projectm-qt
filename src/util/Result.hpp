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

// Result type - either value or error.
// [[nodiscard]] at the class level flags every discarded Result return
// (values AND errors) at all call sites.
//
// Deliberately small: this is an error channel, not a functional-programming
// toolkit. The unused monadic surface and the non-compiling TRY/TRY_VOID
// macros were deleted rather than repaired (T0061). New code that wants
// combinators uses std::expected directly.
template <typename T>
class [[nodiscard]] Result {
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
};

// Specialization for void
template <>
class [[nodiscard]] Result<void> {
    std::optional<Error> error_;

    // Returned when a caller asks for error() on a successful Result. Callers
    // are expected to guard with isErr(); this exists so the unguarded path is
    // a defined empty value rather than UB (T0061).
    static const Error& emptyError() {
        static const Error kEmpty{};
        return kEmpty;
    }

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
        return error_ ? *error_ : emptyError();
    }
};

} // namespace vc