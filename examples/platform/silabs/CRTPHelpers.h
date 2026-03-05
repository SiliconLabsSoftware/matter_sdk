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
  * @brief Common CRTP helper macros for API override with signature validation
  * 
  * These macros implement the "Impl" pattern where:
  * - Public methods in CRTP base call private Impl methods
  * - Base class provides default Impl implementations as fallbacks
  * - Derived classes implement Impl methods (not public methods)
  * - Automatic fallback to base Impl if derived doesn't override
  * - Compile-time signature validation
  * 
  * Pattern inspired by:
  *   ReturnAndVerifyConstExprWithAssert - asserts derived must implement
  *   ReturnAndVerifyConstExpr - allows fallback to base
  */
 
 /**
  * @brief Macro for methods that return a value (non-void) using Impl pattern
  * 
  * Calls Derived::funcImpl if it exists, otherwise falls back to CRTPBase::funcImpl.
  * Validates signature at compile time.
  * 
  * @param CRTPBase The CRTP base class (e.g., LightingManagerCRTP)
  * @param Derived The derived class type
  * @param func The function name (without Impl suffix)
  * @param ... The arguments to pass to the function
  */
 #define CRTP_RETURN_AND_VERIFY(CRTPBase, Derived, func, ...) \
     do { \
         if constexpr (std::is_base_of_v<CRTPBase<Derived>, Derived>) { \
             /* Check if Derived implements funcImpl */ \
             if constexpr (!std::is_same_v<decltype(&Derived::func##Impl), decltype(&CRTPBase<Derived>::func##Impl)>) { \
                 /* Different types means overridden - use derived */ \
                 return static_cast<Derived *>(this)->func##Impl(__VA_ARGS__); \
             } else { \
                 /* Same types means not overridden - use base Impl */ \
                 return static_cast<CRTPBase<Derived> *>(this)->func##Impl(__VA_ARGS__); \
             } \
         } else { \
             /* Not using CRTP, should not reach here */ \
             static_assert(!std::is_base_of_v<CRTPBase<Derived>, Derived>, "Must use CRTP pattern"); \
         } \
     } while(0)
 
 /**
  * @brief Macro for void return type methods using Impl pattern
  * 
  * Same as CRTP_RETURN_AND_VERIFY but for void return types.
  */
 #define CRTP_VOID_AND_VERIFY(CRTPBase, Derived, func, ...) \
     do { \
         if constexpr (std::is_base_of_v<CRTPBase<Derived>, Derived>) { \
             /* Check if Derived implements funcImpl */ \
             if constexpr (!std::is_same_v<decltype(&Derived::func##Impl), decltype(&CRTPBase<Derived>::func##Impl)>) { \
                 /* Different types means overridden - use derived */ \
                 static_cast<Derived *>(this)->func##Impl(__VA_ARGS__); \
             } else { \
                 /* Same types means not overridden - use base Impl */ \
                 static_cast<CRTPBase<Derived> *>(this)->func##Impl(__VA_ARGS__); \
             } \
         } else { \
             /* Not using CRTP, should not reach here */ \
             static_assert(!std::is_base_of_v<CRTPBase<Derived>, Derived>, "Must use CRTP pattern"); \
         } \
     } while(0)
 
 /**
  * @brief Macro for static methods that return a value using Impl pattern
  */
 #define CRTP_STATIC_RETURN_AND_VERIFY(CRTPBase, Derived, func, ...) \
     do { \
         if constexpr (std::is_base_of_v<CRTPBase<Derived>, Derived>) { \
             /* Check if Derived implements funcImpl */ \
             if constexpr (!std::is_same_v<decltype(&Derived::func##Impl), decltype(&CRTPBase<Derived>::func##Impl)>) { \
                 /* Different types means overridden - use derived */ \
                 return Derived::func##Impl(__VA_ARGS__); \
             } else { \
                 /* Same types means not overridden - use base Impl */ \
                 return CRTPBase<Derived>::func##Impl(__VA_ARGS__); \
             } \
         } else { \
             /* Not using CRTP, should not reach here */ \
             static_assert(!std::is_base_of_v<CRTPBase<Derived>, Derived>, "Must use CRTP pattern"); \
         } \
     } while(0)
 
 /**
  * @brief Macro for static void methods using Impl pattern
  */
 #define CRTP_STATIC_VOID_AND_VERIFY(CRTPBase, Derived, func, ...) \
     do { \
         if constexpr (std::is_base_of_v<CRTPBase<Derived>, Derived>) { \
             /* Check if Derived implements funcImpl */ \
             if constexpr (!std::is_same_v<decltype(&Derived::func##Impl), decltype(&CRTPBase<Derived>::func##Impl)>) { \
                 /* Different types means overridden - use derived */ \
                 Derived::func##Impl(__VA_ARGS__); \
             } else { \
                 /* Same types means not overridden - use base Impl */ \
                 CRTPBase<Derived>::func##Impl(__VA_ARGS__); \
             } \
         } else { \
             /* Not using CRTP, should not reach here */ \
             static_assert(!std::is_base_of_v<CRTPBase<Derived>, Derived>, "Must use CRTP pattern"); \
         } \
     } while(0)
 
 /**
  * @brief Macro for methods where base Impl doesn't exist (e.g., private base method)
  * 
  * This version always calls Derived::funcImpl() without checking for base Impl.
  * Use this when the base class method is private or doesn't have an Impl version.
  * The compiler will error if Derived doesn't implement funcImpl.
  * 
  * @param CRTPBase The CRTP base class
  * @param Derived The derived class type
  * @param func The function name (without Impl suffix)
  * @param ... The arguments to pass to the function
  */
 #define CRTP_RETURN_DERIVED_ONLY(CRTPBase, Derived, func, ...) \
     do { \
         if constexpr (std::is_base_of_v<CRTPBase<Derived>, Derived>) { \
             /* Always call derived - compiler validates signature */ \
             return static_cast<Derived *>(this)->func##Impl(__VA_ARGS__); \
         } else { \
             /* Not using CRTP, should not reach here */ \
             static_assert(!std::is_base_of_v<CRTPBase<Derived>, Derived>, "Must use CRTP pattern"); \
         } \
     } while(0)
 
 /**
  * @brief Macro for void methods where base Impl doesn't exist
  */
 #define CRTP_VOID_DERIVED_ONLY(CRTPBase, Derived, func, ...) \
     do { \
         if constexpr (std::is_base_of_v<CRTPBase<Derived>, Derived>) { \
             /* Always call derived - compiler validates signature */ \
             static_cast<Derived *>(this)->func##Impl(__VA_ARGS__); \
         } else { \
             /* Not using CRTP, should not reach here */ \
             static_assert(!std::is_base_of_v<CRTPBase<Derived>, Derived>, "Must use CRTP pattern"); \
         } \
     } while(0)
 