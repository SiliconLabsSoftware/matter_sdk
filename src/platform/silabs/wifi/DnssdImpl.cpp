#include "lib/dnssd/platform/Dnssd.h"
#include "platform/CHIPDeviceLayer.h"
extern "C" {
#include "sl_mdns.h"
}
#include "silabs_utils.h"
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace ::chip::DeviceLayer;

namespace chip {
namespace Dnssd {

// Global mDNS instance
static sl_mdns_t gMdnsInstance;

CHIP_ERROR ChipDnssdInit(DnssdAsyncReturnCallback initCallback, DnssdAsyncReturnCallback errorCallback, void * context)
{
    sl_mdns_configuration_t config = { .protocol = SL_MDNS_PROTO_UDP, .type = SL_IPV6_VERSION, .host_name = "chip-device.local" };

    SILABS_LOG("INIT MDNS");
    sl_status_t status = sl_mdns_init(&gMdnsInstance, &config, nullptr);
    if (status != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "Failed to initialize mDNS: 0x%lx", status);
        if (errorCallback)
        {
            errorCallback(context, CHIP_ERROR_INTERNAL);
        }
        return CHIP_ERROR_INTERNAL;
    }
    SILABS_LOG("INIT MDNS PASS");
    status = sl_mdns_add_interface(&gMdnsInstance, SL_NET_WIFI_CLIENT_INTERFACE);
    if (status != SL_STATUS_OK)
    {
        SILABS_LOG("\r\nFailed to add interface to MDNS : 0x%lx\r\n", status);
        return CHIP_ERROR_INTERNAL;
    }
    SILABS_LOG("\r\nInterface Added to MDNS\r\n");

    if (initCallback)
    {
        initCallback(context, CHIP_NO_ERROR);
    }

    return CHIP_NO_ERROR;
}

void ChipDnssdShutdown()
{
    SILABS_LOG("DEINIT MDNS");

    sl_status_t status = sl_mdns_deinit(&gMdnsInstance);
    if (status != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "Failed to deinitialize mDNS: %ld", status);
    }
    SILABS_LOG("DEINIT MDNS");
}

const char * GetProtocolString(DnssdServiceProtocol protocol)
{
    return protocol == DnssdServiceProtocol::kDnssdProtocolTcp ? "_tcp" : "_udp";
}

sl_mdns_service_t mdnsService = {};
std::string serviceMessage;

CHIP_ERROR ChipDnssdPublishService(const DnssdService * service, DnssdPublishCallback callback, void * context)
{
    sl_mdns_t mdns;

    if (service == NULL)
    {
        SILABS_LOG("Publishing the service again");
        ChipDnssdRemoveServices();
        sl_status_t status = sl_mdns_register_service(&gMdnsInstance, SL_NET_WIFI_CLIENT_INTERFACE, &mdnsService);
        if (status != SL_STATUS_OK)
        {
            ChipLogError(DeviceLayer, "Failed to publish service: 0x%lx", status);
            return CHIP_ERROR_INTERNAL;
        }
        return CHIP_NO_ERROR;
    }
    char service_type[256]; // Allocate enough memory for the concatenated string
    snprintf(service_type, sizeof(service_type), "%s.%s.local.", service->mType, GetProtocolString(service->mProtocol));

    char instance_name[512]; // Allocate enough memory for the concatenated string
    snprintf(instance_name, sizeof(instance_name), "%s.%s.%s.local", service->mName, service->mType,
             GetProtocolString(service->mProtocol));
    // snprintf(instance_name, sizeof(instance_name), "%s.%s", service->mName, service_type);

    mdnsService.instance_name = strdup(instance_name); // Duplicate the string to ensure memory safety
    SILABS_LOG("%s", instance_name);
    SILABS_LOG("%s", service_type);
    mdnsService.service_type = strdup(service_type); // Duplicate the string to ensure memory safety
    mdnsService.port         = service->mPort;
    mdnsService.ttl          = 300; // Default TTL; adjust as needed
    // VerifyOrExit(service->mTextEntrySize <= UINT8_MAX, error = CHIP_ERROR_INVALID_ARGUMENT);
    if (service->mTextEntries && service->mTextEntrySize > 0)
    {
        // std::string serviceMessage;
        for (size_t i = 0; i < service->mTextEntrySize; i++)
        {
            if (service->mTextEntries[i].mKey == nullptr || service->mTextEntries[i].mData == nullptr)
            {
                continue;
            }
            SILABS_LOG("KEY = %s      :::::     Value = %s", service->mTextEntries[i].mKey,
                       reinterpret_cast<const char *>(service->mTextEntries[i].mData));
            serviceMessage += std::string(service->mTextEntries[i].mKey) + "=" +
                std::string(reinterpret_cast<const char *>(service->mTextEntries[i].mData)) + " ";
            SILABS_LOG("Service Message = %s", serviceMessage.c_str());
        }
        mdnsService.service_message = strdup(serviceMessage.c_str()); // Duplicate the string to ensure memory safety
    }
    SILABS_LOG("Port = %d", mdnsService.port);
    sl_status_t status = sl_mdns_register_service(&gMdnsInstance, SL_NET_WIFI_CLIENT_INTERFACE, &mdnsService);
    if (status != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "Failed to publish service: 0x%lx", status);
        return CHIP_ERROR_INTERNAL;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR ChipDnssdRemoveServices()
{
    // Currently, `sl_mdns_unregister_service` is not supported in `sl_mdns.c`.
    // As a workaround, we can deinitialize and reinitialize the mDNS instance to remove all services.
    sl_mdns_deinit(&gMdnsInstance);

    sl_mdns_configuration_t config = { .protocol = SL_MDNS_PROTO_UDP, .type = SL_IPV6_VERSION, .host_name = "chip-device.local" };

    sl_status_t status = sl_mdns_init(&gMdnsInstance, &config, nullptr);
    if (status != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "Failed to reinitialize mDNS: %ld", status);
        return CHIP_ERROR_INTERNAL;
    }

    status = sl_mdns_add_interface(&gMdnsInstance, SL_NET_WIFI_CLIENT_INTERFACE);
    if (status != SL_STATUS_OK)
    {
        SILABS_LOG("\r\nFailed to add interface to MDNS : 0x%lx\r\n", status);
        return CHIP_ERROR_INTERNAL;
    }
    SILABS_LOG("\r\nInterface Added to MDNS\r\n");

    return CHIP_NO_ERROR;
}

CHIP_ERROR ChipDnssdFinalizeServiceUpdate()
{
    // No explicit finalize function in `sl_mdns.c`. Assume updates are applied immediately.
    return CHIP_NO_ERROR;
}

CHIP_ERROR ChipDnssdBrowse(const char * type, DnssdServiceProtocol protocol, chip::Inet::IPAddressType addressType,
                           chip::Inet::InterfaceId interface, DnssdBrowseCallback callback, void * context,
                           intptr_t * browseIdentifier)
{
    // Currently, `sl_mdns_discover_service` is not supported in `sl_mdns.c`.
    // Return an error indicating that browsing is not implemented.
    ChipLogError(DeviceLayer, "Browsing services is not supported in sl_mdns.c");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR ChipDnssdStopBrowse(intptr_t browseIdentifier)
{
    // Currently, stopping a browse operation is not supported in `sl_mdns.c`.
    // Return an error indicating that stopping browse is not implemented.
    ChipLogError(DeviceLayer, "Stopping browse is not supported in sl_mdns.c");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR ChipDnssdResolve(DnssdService * service, chip::Inet::InterfaceId interface, DnssdResolveCallback callback,
                            void * context)
{
    // Currently, `sl_mdns_discover_service` is not supported in `sl_mdns.c`.
    // Return an error indicating that resolving is not implemented.
    ChipLogError(DeviceLayer, "Resolving services is not supported in sl_mdns.c");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

void ChipDnssdResolveNoLongerNeeded(const char * instanceName)
{
    // No explicit function in `sl_mdns.c` to handle this. Assume no action is needed.
}

CHIP_ERROR ChipDnssdReconfirmRecord(const char * hostname, chip::Inet::IPAddress address, chip::Inet::InterfaceId interface)
{
    // Currently, `sl_mdns.c` does not support reconfirming records.
    // Return an error indicating that reconfirming is not implemented.
    ChipLogError(DeviceLayer, "Reconfirming records is not supported in sl_mdns.c");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

} // namespace Dnssd
} // namespace chip
