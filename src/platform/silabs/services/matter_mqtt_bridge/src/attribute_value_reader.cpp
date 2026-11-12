/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

#include "attribute_value_reader.h"

#include <access/SubjectDescriptor.h>
#include <app/AttributeEncodeState.h>
#include <app/AttributeValueEncoder.h>
#include <app/ConcreteClusterPath.h>
#include <app/MessageDef/AttributeDataIB.h>
#include <app/MessageDef/AttributeReportIB.h>
#include <app/MessageDef/ReportDataMessage.h>
#include <app/data-model-provider/ActionReturnStatus.h>
#include <app/data-model-provider/OperationTypes.h>
#include <app/server-cluster/ServerClusterInterface.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/core/TLV.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

namespace {

// Encodes a single-attribute ReadAttribute response and maps the first primitive TLV value to RawValue.

using namespace chip;
using namespace chip::app;
using namespace chip::TLV;

constexpr size_t kTlvBufferSize = 128;

// Only primitive attribute scalars are exported over MQTT; structures/lists are dropped.
CHIP_ERROR MapTlvToRaw(TLVReader & reader, MatterMqttBridge::RawValue & out)
{
    switch (reader.GetType())
    {
    case TLVType::kTLVType_Boolean: {
        bool value = false;
        ReturnErrorOnFailure(reader.Get(value));
        out.type            = MatterMqttBridge::RawValueType::kBool;
        out.data.boolean    = value;
        return CHIP_NO_ERROR;
    }
    case TLVType::kTLVType_SignedInteger: {
        int64_t value = 0;
        ReturnErrorOnFailure(reader.Get(value));
        out.type              = MatterMqttBridge::RawValueType::kSigned;
        out.data.signedValue  = static_cast<int32_t>(value);
        return CHIP_NO_ERROR;
    }
    case TLVType::kTLVType_UnsignedInteger: {
        uint64_t value = 0;
        ReturnErrorOnFailure(reader.Get(value));
        out.type                = MatterMqttBridge::RawValueType::kUnsigned;
        out.data.unsignedValue  = static_cast<uint32_t>(value);
        return CHIP_NO_ERROR;
    }
    case TLVType::kTLVType_Null: {
        out.type = MatterMqttBridge::RawValueType::kNull;
        return CHIP_NO_ERROR;
    }
    default:
        ChipLogDetail(AppServer, "MatterMqttBridge: non-primitive attribute skipped");
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }
}

CHIP_ERROR DecodeFirstAttributeData(ByteSpan tlv, MatterMqttBridge::RawValue & out)
{
    // Walk the TLV produced by EncodeRead: outer ReportDataMessage-shaped structure.
    TLVReader reader;
    reader.Init(tlv);

    ReturnErrorOnFailure(reader.Next());
    VerifyOrReturnError(reader.GetType() == TLVType::kTLVType_Structure, CHIP_ERROR_INVALID_ARGUMENT);
    TLVType structType;
    ReturnErrorOnFailure(reader.EnterContainer(structType));

    // AttributeReportIBs array (we only ever encode a single report).
    ReturnErrorOnFailure(reader.Next());
    VerifyOrReturnError(reader.GetType() == TLVType::kTLVType_Array, CHIP_ERROR_INVALID_ARGUMENT);
    TLVType arrayType;
    ReturnErrorOnFailure(reader.EnterContainer(arrayType));

    ReturnErrorOnFailure(reader.Next());
    AttributeReportIB::Parser report;
    ReturnErrorOnFailure(report.Init(reader));

    // AttributeDataIB holds the scalar value TLV we forward to MQTT JSON.
    AttributeDataIB::Parser data;
    ReturnErrorOnFailure(report.GetAttributeData(&data));

    TLVReader dataReader;
    ReturnErrorOnFailure(data.GetData(&dataReader));
    ReturnErrorOnFailure(dataReader.Next());
    return MapTlvToRaw(dataReader, out);
}

enum class EncodeReadBackend
{
    kCodeDrivenRegistry,
    kCodegenProvider,
};

// Build the same TLV shape as a ReadAttribute interaction response, without going over the network.
// DecodeFirstAttributeData then extracts the encoded scalar from this blob.
CHIP_ERROR EncodeRead(const ConcreteAttributePath & path, EncodeReadBackend backend, uint8_t * buffer, size_t bufferLen,
                      size_t & outLen)
{
    TLVWriter writer;
    writer.Init(buffer, bufferLen);

    // ReportDataMessage { AttributeReportIBs = [ ... ] }
    TLVType outerType;
    ReturnErrorOnFailure(writer.StartContainer(AnonymousTag(), TLVType::kTLVType_Structure, outerType));

    AttributeReportIBs::Builder reportsBuilder;
    ReturnErrorOnFailure(reportsBuilder.Init(&writer, to_underlying(ReportDataMessage::Tag::kAttributeReportIBs)));

    // AttributeValueEncoder fills one AttributeReportIB for @p path.
    chip::Access::SubjectDescriptor subject;
    DataModel::ReadAttributeRequest request(path, subject);
    AttributeValueEncoder encoder(reportsBuilder, request.subjectDescriptor, request.path, 0, false,
                                  AttributeEncodeState());

    // Route the read to code-driven ServerClusterInterface or legacy codegen provider.
    DataModel::ActionReturnStatus status(CHIP_NO_ERROR);
    if (backend == EncodeReadBackend::kCodegenProvider)
    {
        status = CodegenDataModelProvider::Instance().ReadAttribute(request, encoder);
    }
    else
    {
        ServerClusterInterface * cluster =
            CodegenDataModelProvider::Instance().Registry().Get(ConcreteClusterPath(path.mEndpointId, path.mClusterId));
        VerifyOrReturnError(cluster != nullptr, CHIP_ERROR_NOT_FOUND);
        status = cluster->ReadAttribute(request, encoder);
    }

    if (status.IsError())
    {
        return status.GetUnderlyingError();
    }

    ReturnErrorOnFailure(reportsBuilder.EndOfAttributeReportIBs());
    ReturnErrorOnFailure(writer.EndContainer(outerType));
    ReturnErrorOnFailure(writer.Finalize());
    outLen = writer.GetLengthWritten();
    return CHIP_NO_ERROR;
}

// Prefer code-driven ServerClusterInterface; fall back to codegen ReadAttribute for Ember-backed clusters.
CHIP_ERROR EncodeReadBestEffort(const ConcreteAttributePath & path, uint8_t * buffer, size_t bufferLen, size_t & outLen)
{
    ServerClusterInterface * cluster =
        CodegenDataModelProvider::Instance().Registry().Get(ConcreteClusterPath(path.mEndpointId, path.mClusterId));
    const EncodeReadBackend backend =
        cluster != nullptr ? EncodeReadBackend::kCodeDrivenRegistry : EncodeReadBackend::kCodegenProvider;
    return EncodeRead(path, backend, buffer, bufferLen, outLen);
}

} // namespace

// Synthesize a minimal ReadAttribute response TLV, then peel out the first data value.
CHIP_ERROR CodeDrivenAttributeReader::Read(const chip::app::ConcreteAttributePath & path, MatterMqttBridge::RawValue & out)
{
    uint8_t tlv[kTlvBufferSize];
    size_t tlvLen = 0;
    ReturnErrorOnFailure(EncodeReadBestEffort(path, tlv, sizeof(tlv), tlvLen));
    return DecodeFirstAttributeData(ByteSpan(tlv, tlvLen), out);
}
