/*******************************************************************************
 * @file AppleKeychainHandler.h
 * @brief Handler for the Apple Keychain edge-case processing logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#pragma once

#include <VendorHandler.h>
#include <lib/core/DataModelTypes.h>

namespace chip {
namespace app {
namespace Silabs {

/**
 * @brief Apple Keychain edge-case processing handler.
 *        Handler is called when we validate if the Apple Keychain fabric has an active subscription which it never does.
 *        In this case, we validate if the main Apple fabric has an active subscription.
 *
 */
class AppleKeychainHandler : public VendorHandler<AppleKeychainHandler>
{
public:
    static bool ProcessVendorCaseImpl(chip::app::SubscriptionsInfoProvider * subscriptionsInfoProvider,
                                      chip::FabricTable * fabricTable)
    {
        VerifyOrReturnValue(subscriptionsInfoProvider != nullptr && fabricTable != nullptr, false);

        bool hasValidException = false;

        for (auto it = fabricTable->begin(); it != fabricTable->end(); ++it)
        {
            if ((it->GetVendorId() == chip::VendorId::Apple) &&
                subscriptionsInfoProvider->FabricHasAtLeastOneActiveSubscription(it->GetFabricIndex()))
            {
                hasValidException = true;
                break;
            }
        }

        return hasValidException;
    }

    static bool IsMatchingVendorID(chip::VendorId vendorId) { return vendorId == kAppleKeychainVendorId; }

private:
    // Official Apple Keychain vendor ID from the CSA database
    static constexpr uint16_t kAppleKeychainVendorId = 4996;
};

} // namespace Silabs
} // namespace app
} // namespace chip
