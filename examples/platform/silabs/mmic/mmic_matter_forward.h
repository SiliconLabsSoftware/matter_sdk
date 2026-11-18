/*******************************************************************************
 * @file
 * @brief Typed C++ helpers to forward Matter cluster commands and attribute changes
 *        to the host over MMIC, with parameters encoded as Matter TLV.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
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

#include "mmic_forward.h"

#include <app/data-model/Encode.h>
#include <app/data-model/List.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <lib/core/TLV.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ScopedMemoryBuffer.h>
#include <lib/support/Span.h>
#include <protocols/interaction_model/StatusCode.h>
#include <system/SystemClock.h>

#include <type_traits>
#include <utility>

// Size of the buffer used to TLV-encode forwarded parameters.
#ifndef SL_MATTER_MMIC_FORWARD_MAX_PARAMS_SIZE
#define SL_MATTER_MMIC_FORWARD_MAX_PARAMS_SIZE 2048
#endif

namespace chip {
namespace Silabs {
namespace Mmic {

inline constexpr size_t kMaxParamsSize = SL_MATTER_MMIC_FORWARD_MAX_PARAMS_SIZE;
static_assert(kMaxParamsSize <= UINT16_MAX - MMIC_PACKET_OVERHEAD - sizeof(clusterCommandHeader_t),
              "SL_MATTER_MMIC_FORWARD_MAX_PARAMS_SIZE too large");

namespace detail {

template <typename T, typename = void>
struct HasEncodeForRead : std::false_type
{
};

template <typename T>
struct HasEncodeForRead<T,
                        std::void_t<decltype(std::declval<const T &>().EncodeForRead(
                            std::declval<TLV::TLVWriter &>(), TLV::AnonymousTag(), std::declval<const T &>().GetFabricIndex()))>>
    : std::true_type
{
};

template <typename T>
CHIP_ERROR EncodeElement(TLV::TLVWriter & writer, const T & value)
{
    if constexpr (HasEncodeForRead<T>::value)
    {
        return value.EncodeForRead(writer, TLV::AnonymousTag(), value.GetFabricIndex());
    }
    else
    {
        return app::DataModel::Encode(writer, TLV::AnonymousTag(), value);
    }
}

} // namespace detail

inline CHIP_ERROR ToChipError(uint8_t mmicError)
{
    switch (mmicError)
    {
    case MMIC_ERROR_OK:
        return CHIP_NO_ERROR;
    case MMIC_ERROR_INVALID_ARG:
        return CHIP_ERROR_INVALID_ARGUMENT;
    case MMIC_ERROR_NO_MEMORY:
        return CHIP_ERROR_NO_MEMORY;
    case MMIC_ERROR_NOT_READY:
        return CHIP_ERROR_INCORRECT_STATE;
    case MMIC_ERROR_TIMEOUT:
        return CHIP_ERROR_TIMEOUT;
    case MMIC_ERROR_BUSY:
        return CHIP_ERROR_BUSY;
    default:
        return CHIP_ERROR_INTERNAL;
    }
}

/// Encodes each value as an anonymous TLV element, in order. buffer is reduced to the encoded size.
/// Fabric-scoped structures are encoded as read by their own fabric, i.e. with all their fields.
template <typename... Ts>
CHIP_ERROR EncodeTlv(MutableByteSpan & buffer, const Ts &... values)
{
    TLV::TLVWriter writer;
    writer.Init(buffer);
    CHIP_ERROR err = CHIP_NO_ERROR;
    ((err = (err == CHIP_NO_ERROR) ? detail::EncodeElement(writer, values) : err), ...);
    ReturnErrorOnFailure(err);
    ReturnErrorOnFailure(writer.Finalize());
    buffer.reduce_size(writer.GetLengthWritten());
    return CHIP_NO_ERROR;
}

/// Forwards a cluster command to the host without waiting for an answer.
/// params are encoded, in order, as anonymous TLV elements.
template <typename... Ts>
CHIP_ERROR SendClusterCommand(EndpointId endpoint, ClusterId cluster, CommandId command, const Ts &... params)
{
    Platform::ScopedMemoryBuffer<uint8_t> buffer;
    VerifyOrReturnError(buffer.Alloc(kMaxParamsSize), CHIP_ERROR_NO_MEMORY);
    MutableByteSpan encoded(buffer.Get(), kMaxParamsSize);
    ReturnErrorOnFailure(EncodeTlv(encoded, params...));
    return ToChipError(
        mmic_send_cluster_command(endpoint, cluster, command, encoded.data(), static_cast<uint16_t>(encoded.size())));
}

/// Forwards a cluster command and blocks until the host answers or the timeout elapses.
/// On success, hostStatus holds the status returned by the host and response is reduced
/// to the TLV response data it returned.
template <typename... Ts>
CHIP_ERROR RequestClusterCommand(EndpointId endpoint, ClusterId cluster, CommandId command, System::Clock::Milliseconds32 timeout,
                                 Protocols::InteractionModel::Status & hostStatus, MutableByteSpan & response,
                                 const Ts &... params)
{
    VerifyOrReturnError(response.size() <= UINT16_MAX, CHIP_ERROR_INVALID_ARGUMENT);

    Platform::ScopedMemoryBuffer<uint8_t> buffer;
    VerifyOrReturnError(buffer.Alloc(kMaxParamsSize), CHIP_ERROR_NO_MEMORY);
    MutableByteSpan encoded(buffer.Get(), kMaxParamsSize);
    ReturnErrorOnFailure(EncodeTlv(encoded, params...));

    uint8_t status       = 0;
    uint16_t responseLen = 0;
    ReturnErrorOnFailure(ToChipError(mmic_request_cluster_command(
        endpoint, cluster, command, encoded.data(), static_cast<uint16_t>(encoded.size()), timeout.count(), &status,
        response.data(), static_cast<uint16_t>(response.size()), &responseLen)));

    hostStatus = static_cast<Protocols::InteractionModel::Status>(status);
    response.reduce_size(responseLen);
    return CHIP_NO_ERROR;
}

/// Notifies the host that an attribute changed, with its new value encoded as one anonymous TLV element.
template <typename T>
CHIP_ERROR SendAttributeChanged(EndpointId endpoint, ClusterId cluster, AttributeId attribute, const T & value)
{
    Platform::ScopedMemoryBuffer<uint8_t> buffer;
    VerifyOrReturnError(buffer.Alloc(kMaxParamsSize), CHIP_ERROR_NO_MEMORY);
    MutableByteSpan encoded(buffer.Get(), kMaxParamsSize);
    ReturnErrorOnFailure(EncodeTlv(encoded, value));
    return ToChipError(
        mmic_send_attribute_changed(endpoint, cluster, attribute, encoded.data(), static_cast<uint16_t>(encoded.size())));
}

/// Notifies the host that an attribute changed, without providing its value.
inline CHIP_ERROR SendAttributeChanged(EndpointId endpoint, ClusterId cluster, AttributeId attribute)
{
    return ToChipError(mmic_send_attribute_changed(endpoint, cluster, attribute, nullptr, 0));
}

} // namespace Mmic
} // namespace Silabs
} // namespace chip
