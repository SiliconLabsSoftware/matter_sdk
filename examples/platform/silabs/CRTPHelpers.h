/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <tuple>
#include <type_traits>

/**
 * CRTP macros: Base = CRTP base (template), Derived = concrete type, func = name (Impl suffix added).
 * All dispatch macros assert that Derived::func has the same signature as Base::func (return type and parameters).
 *
 * Common errors:
 * - "no member named 'X'": Derived must define X() or Base must provide a default in its private section.
 * - "invalid static_cast": Derived must inherit from Base (e.g. class Derived : public AppTaskImpl<Derived>).
 * - "must have the same signature": change Derived::func to match Base::func exactly (return type and parameter types).
 * - "GetAppTask" / "sAppTask": Derived must define static GetAppTask() and the singleton instance.
 */

namespace crtp {
/** Extracts return type and parameter types from a pointer-to-member-function for signature comparison. */
template <typename T>
struct MemFnSig;
template <typename R, typename C, typename... Args>
struct MemFnSig<R (C::*)(Args...)> {
    using return_type = R;
    using args_tuple  = std::tuple<Args...>;
};
template <typename R, typename C, typename... Args>
struct MemFnSig<R (C::*)(Args...) const> {
    using return_type = R;
    using args_tuple  = std::tuple<Args...>;
};

template <typename BaseImpl, typename DerivedImpl>
constexpr bool kSameImplSignature =
    std::is_same<typename MemFnSig<BaseImpl>::return_type, typename MemFnSig<DerivedImpl>::return_type>::value &&
    std::is_same<typename MemFnSig<BaseImpl>::args_tuple, typename MemFnSig<DerivedImpl>::args_tuple>::value;
} // namespace crtp

/** Fails at compile time if Derived::func does not have the same signature as Base::func. */
#define CRTP_SAME_IMPL_SIGNATURE(Base, Derived, func) \
    static_assert( \
        crtp::kSameImplSignature<decltype(&Base::func), decltype(&Derived::func)>, \
        #Derived "::" #func "Impl() must have the same signature as " #Base "::" #func "Impl() (same return type and parameter types)")

/**
 * Cast this to Derived*. If you see "invalid static_cast": ensure Derived inherits from the CRTP base
 * (e.g. class Derived : public AppTaskImpl<Derived>).
 */
#define CRTP_THIS(Derived) static_cast<Derived *>(this)

/**
 * Get app task singleton as Derived&. Derived must define static GetAppTask() and the singleton.
 * If you see "GetAppTask" or "sAppTask" errors: add "static Derived& GetAppTask();" and "static Derived sAppTask;" in Derived.
 */
#define CRTP_APP_TASK(Derived) static_cast<Derived &>(Derived::GetAppTask())

/**
 * Dispatch to Derived::func(); no default in base. Derived must implement (override) func().
 */
#define CRTP_RETURN_DERIVED_ONLY(Base, Derived, func) \
    do { \
        static_assert(std::is_base_of_v<Base, Derived>, \
            "CRTP_RETURN_DERIVED_ONLY: " #Derived " must inherit from " #Base " (e.g. class " #Derived " : public " #Base "<" #Derived ">)."); \
        CRTP_SAME_IMPL_SIGNATURE(Base, Derived, func); \
        if constexpr (std::is_base_of_v<Base, Derived>) { \
            static_assert( \
                !std::is_same<decltype(&Derived::func), decltype(&Base::func)>::value, \
                "CRTP_RETURN_DERIVED_ONLY: " #Derived " must implement " #func "Impl() (no default in base)."); \
            return static_cast<Derived *>(this)->func(); \
        } else { \
            return this->func(); \
        } \
    } while (0)

/**
 * Dispatch to Derived::func(); fallback to base default when not in CRTP context.
 */
#define CRTP_RETURN_AND_VERIFY(Base, Derived, func) \
    do { \
        static_assert(std::is_base_of_v<Base, Derived>, \
            "CRTP_RETURN_AND_VERIFY: " #Derived " must inherit from " #Base ". " \
            "If you see 'no member named " #func "Impl': add " #func "Impl() in " #Derived " or a default in " #Base " private section."); \
        CRTP_SAME_IMPL_SIGNATURE(Base, Derived, func); \
        if constexpr (std::is_base_of_v<Base, Derived>) { \
            return static_cast<Derived *>(this)->func(); \
        } else { \
            return this->func(); \
        } \
    } while (0)

#define CRTP_RETURN_AND_VERIFY_ARGS(Base, Derived, func, ...) \
    do { \
        static_assert(std::is_base_of_v<Base, Derived>, \
            "CRTP_RETURN_AND_VERIFY_ARGS: " #Derived " must inherit from " #Base ". " \
            "If 'no member named " #func "Impl': add " #func "Impl(...) in " #Derived " or a default in " #Base "."); \
        CRTP_SAME_IMPL_SIGNATURE(Base, Derived, func); \
        if constexpr (std::is_base_of_v<Base, Derived>) { \
            return static_cast<Derived *>(this)->func(__VA_ARGS__); \
        } else { \
            return this->func(__VA_ARGS__); \
        } \
    } while (0)

#define CRTP_RETURN_CONST_AND_VERIFY_ARGS(Base, Derived, func, ...) \
    do { \
        static_assert(std::is_base_of_v<Base, Derived>, \
            "CRTP_RETURN_CONST_AND_VERIFY_ARGS: " #Derived " must inherit from " #Base ". " \
            "If 'no member named " #func "Impl': add " #func "Impl(...) const in " #Derived " or a default in " #Base "."); \
        CRTP_SAME_IMPL_SIGNATURE(Base, Derived, func); \
        if constexpr (std::is_base_of_v<Base, Derived>) { \
            return static_cast<const Derived *>(this)->func(__VA_ARGS__); \
        } else { \
            return this->func(__VA_ARGS__); \
        } \
    } while (0)

#define CRTP_VOID_AND_VERIFY(Base, Derived, func, ...) \
    do { \
        static_assert(std::is_base_of_v<Base, Derived>, \
            "CRTP_VOID_AND_VERIFY: " #Derived " must inherit from " #Base ". " \
            "If 'no member named " #func "Impl': add " #func "Impl(...) in " #Derived " or a default in " #Base "."); \
        CRTP_SAME_IMPL_SIGNATURE(Base, Derived, func); \
        if constexpr (std::is_base_of_v<Base, Derived>) { \
            static_cast<Derived *>(this)->func(__VA_ARGS__); \
        } else { \
            this->func(__VA_ARGS__); \
        } \
    } while (0)

/**
 * Static dispatch via Derived::GetAppTask().func(...). No fallback (no this).
 */
#define CRTP_STATIC_VOID_AND_VERIFY(Base, Derived, func, ...) \
    do { \
        static_assert(std::is_base_of_v<Base, Derived>, \
            "CRTP_STATIC_VOID_AND_VERIFY: " #Derived " must inherit from " #Base ". " \
            #Derived " must define static GetAppTask() and " #func "Impl(...)."); \
        CRTP_SAME_IMPL_SIGNATURE(Base, Derived, func); \
        if constexpr (std::is_base_of_v<Base, Derived>) { \
            static_cast<Derived &>(Derived::GetAppTask()).func(__VA_ARGS__); \
        } \
    } while (0)
