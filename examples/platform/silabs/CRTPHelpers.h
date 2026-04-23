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

#include <type_traits>

/**
 * CRTP macros for compile-time override dispatch with optional signature validation.
 * Base = CRTP base class, Derived = concrete class, func = method name (Impl suffix added for dispatch).
 *
 * For optional overrides (methods with a base default), CRTP_CHECK_OPTIONAL_IMPL fires only when
 * Derived provides an override with a mismatched signature. When Derived does not override at all,
 * the check is skipped (the first is_same condition is true, short-circuiting the OR).
 */

/** Strip class type from a member function pointer, keeping return type, args, and const qualifier. */
template <typename>
struct mfp_sig;
template <typename C, typename Ret, typename... Args>
struct mfp_sig<Ret (C::*)(Args...)>
{
    using type = Ret(Args...);
};
template <typename C, typename Ret, typename... Args>
struct mfp_sig<Ret (C::*)(Args...) const>
{
    using type = Ret(Args...) const;
};

/**
 * Asserts that if Derived overrides func##Impl, its signature matches the base default.
 * Requires Base to have a default func##Impl (i.e., only for optional overrides).
 */
#define CRTP_CHECK_OPTIONAL_IMPL(Base, Derived, func)                                                                              \
    static_assert(std::is_same<decltype(&Derived::func##Impl), decltype(&Base::func##Impl)>::value ||                              \
                      std::is_same<typename mfp_sig<decltype(&Derived::func##Impl)>::type,                                         \
                                   typename mfp_sig<decltype(&Base::func##Impl)>::type>::value,                                    \
                  #Derived "::" #func "Impl() signature does not match " #Base "::" #func "Impl()")

/** Cast this to Derived* (use in instance methods). */
#define CRTP_THIS(Derived) static_cast<Derived *>(this)

/** Get the app task singleton as Derived& (use in static methods that dispatch to Impl). */
#define CRTP_APP_TASK(Derived) static_cast<Derived &>(Derived::GetAppTask())

/** For required overrides only (no base default). Missing Impl produces a raw compiler error. */
#define CRTP_RETURN_DERIVED_ONLY(Base, Derived, func) return static_cast<Derived *>(this)->func##Impl()

#define CRTP_RETURN_AND_VERIFY(Base, Derived, func)                                                                                \
    do                                                                                                                             \
    {                                                                                                                              \
        CRTP_CHECK_OPTIONAL_IMPL(Base, Derived, func);                                                                             \
        return static_cast<Derived *>(this)->func##Impl();                                                                         \
    } while (0)

#define CRTP_RETURN_AND_VERIFY_ARGS(Base, Derived, func, ...)                                                                      \
    do                                                                                                                             \
    {                                                                                                                              \
        CRTP_CHECK_OPTIONAL_IMPL(Base, Derived, func);                                                                             \
        return static_cast<Derived *>(this)->func##Impl(__VA_ARGS__);                                                              \
    } while (0)

#define CRTP_RETURN_CONST_AND_VERIFY_ARGS(Base, Derived, func, ...)                                                                \
    do                                                                                                                             \
    {                                                                                                                              \
        CRTP_CHECK_OPTIONAL_IMPL(Base, Derived, func);                                                                             \
        return static_cast<const Derived *>(this)->func##Impl(__VA_ARGS__);                                                        \
    } while (0)

#define CRTP_VOID_AND_VERIFY(Base, Derived, func, ...)                                                                             \
    do                                                                                                                             \
    {                                                                                                                              \
        CRTP_CHECK_OPTIONAL_IMPL(Base, Derived, func);                                                                             \
        static_cast<Derived *>(this)->func##Impl(__VA_ARGS__);                                                                     \
    } while (0)

#define CRTP_STATIC_VOID_AND_VERIFY(Base, Derived, func, ...)                                                                      \
    do                                                                                                                             \
    {                                                                                                                              \
        CRTP_CHECK_OPTIONAL_IMPL(Base, Derived, func);                                                                             \
        static_cast<Derived &>(Derived::GetAppTask()).func##Impl(__VA_ARGS__);                                                     \
    } while (0)
