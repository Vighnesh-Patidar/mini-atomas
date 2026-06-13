#pragma once

// Component type system — see ARCHITECTURE.md §4.1
//
// Three pieces live here:
//
//   1. ComponentTypeID — uint64 derived at compile time from the type's name
//      via __PRETTY_FUNCTION__ + FNV-1a 64. No RTTI (per §15). Stable across
//      translation units of the same compiler / standard library.
//
//   2. HotComponent<T> / ColdComponent<T> — CRTP tag bases. The hot/cold
//      choice is a *storage strategy hint* (§4.1); both flow through the
//      same unified registration path in EntityRegistry.
//
//   3. ComponentOrigin — Built_In vs User, set by the registration entry
//      point used (privileged vs public). Tamper-resistant within the
//      trusted compute base (§4.1, §13.5).
//
// Compile-time hashing limitations:
//   - Different compilers produce different __PRETTY_FUNCTION__ strings;
//     IDs are stable within one toolchain, not across (Clang vs GCC).
//     v0.1 supports Clang + GCC; MSVC is out of scope.
//   - FNV-1a 64 has astronomically low but non-zero collision probability.
//     EntityRegistry validates uniqueness at registration time (§4.1).

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace mith {

using ComponentTypeID = std::uint64_t;

namespace detail {

// FNV-1a 64-bit hash. constexpr; usable in static_assert.
constexpr std::uint64_t fnv1a_64(std::string_view s) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (char c : s) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Produces a string containing T in a compiler-specific format. The caller
// extracts the type substring via extract_type_name() below.
template<typename T>
constexpr std::string_view pretty_function() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    return __FUNCSIG__;
#else
    #error "mith::detail::pretty_function: unsupported compiler"
#endif
}

// Extract the type name substring from a pretty-function string.
// Looks for "T = " (GCC / Clang) or "pretty_function<" (MSVC fallback).
constexpr std::string_view extract_type_name(std::string_view pretty) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    constexpr std::string_view marker = "T = ";
    const auto start = pretty.find(marker);
    if (start == std::string_view::npos) return pretty;
    const auto begin = start + marker.size();
    auto end = pretty.find_first_of(";]", begin);
    if (end == std::string_view::npos) end = pretty.size();
    return pretty.substr(begin, end - begin);
#elif defined(_MSC_VER)
    constexpr std::string_view marker = "pretty_function<";
    const auto start = pretty.find(marker);
    if (start == std::string_view::npos) return pretty;
    const auto begin = start + marker.size();
    const auto end = pretty.find(">(", begin);
    if (end == std::string_view::npos) return pretty.substr(begin);
    return pretty.substr(begin, end - begin);
#endif
}

} // namespace detail

// Stable compile-time string view of a type's name.
template<typename T>
constexpr std::string_view type_name() noexcept {
    return detail::extract_type_name(detail::pretty_function<T>());
}

// Stable compile-time component ID for T. FNV-1a 64 over type_name<T>().
template<typename T>
constexpr ComponentTypeID component_id() noexcept {
    return detail::fnv1a_64(type_name<T>());
}

// CRTP tag bases.
//
// Component authors inherit from one of these to signal storage strategy.
// Both share the same registration / access API on EntityRegistry; the
// difference is internal (dense slot vs sparse map at v0.1 N=1; archetype
// storage vs map at v0.5+ N>1).
template<typename Derived>
struct HotComponent {
protected:
    HotComponent() noexcept = default;
};

template<typename Derived>
struct ColdComponent {
protected:
    ColdComponent() noexcept = default;
};

// Detection traits.
template<typename T>
inline constexpr bool is_hot_component_v  = std::is_base_of_v<HotComponent<T>, T>;

template<typename T>
inline constexpr bool is_cold_component_v = std::is_base_of_v<ColdComponent<T>, T>;

template<typename T>
inline constexpr bool is_component_v = is_hot_component_v<T> || is_cold_component_v<T>;

// Concept-like helper for SFINAE / static_assert.
template<typename T>
inline constexpr bool is_well_formed_component_v =
    is_component_v<T> && !(is_hot_component_v<T> && is_cold_component_v<T>);

// Origin of a registration. Set by the entry point used:
//   register_component<T>()         → User      (public path)
//   register_builtin_component<T>() → Built_In  (privileged internal path)
//
// User code cannot claim Built_In origin — there is no public overload of
// register_component that takes an origin. See §4.1, §13.5.
enum class ComponentOrigin : std::uint8_t {
    Built_In = 0,
    User     = 1,
};

} // namespace mith
