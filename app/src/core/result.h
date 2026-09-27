#pragma once

#include <QString>

#include <optional>
#include <utility>

namespace omnidict::core {

/// Why a fallible operation failed. The message is for logs and diagnostics;
/// user-facing wording is chosen by the caller.
struct Error
{
    QString message;
};

/// The outcome of a fallible operation: a value or an Error, never both.
/// Stand-in for std::expected until the toolchain baseline reaches C++23.
template <typename T>
class [[nodiscard]] Result
{
public:
    Result(T value) // NOLINT(google-explicit-constructor): `return value;` is the point
        : m_value(std::move(value))
    {}
    Result(Error error) // NOLINT(google-explicit-constructor): `return Error{...};`
        : m_error(std::move(error.message))
    {}

    [[nodiscard]] bool hasValue() const { return m_value.has_value(); }
    explicit operator bool() const { return hasValue(); }

    // The accessors below dereference without checking: hasValue() is their
    // documented precondition, as it is for std::expected.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)

    /// Precondition: hasValue().
    [[nodiscard]] const T& value() const& { return *m_value; }
    [[nodiscard]] T& value() & { return *m_value; }
    /// Moves the value out. Precondition: hasValue().
    [[nodiscard]] T take() { return std::move(*m_value); }

    // NOLINTEND(bugprone-unchecked-optional-access)

    /// Empty when hasValue().
    [[nodiscard]] const QString& error() const { return m_error; }

private:
    std::optional<T> m_value;
    QString m_error;
};

} // namespace omnidict::core
