#include "BLECLICommands.h"

extern "C" {
void HandleHelloWrapper(sl_cli_command_arg_t * arguments)
{
    HandleHello(arguments);
}

void HandleGetAddressWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGetAddress(arguments);
}

void HandlePrintConnectionsWrapper(sl_cli_command_arg_t * arguments)
{
    HandlePrintConnections(arguments);
}

void HandleGapStopAdvertisingWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGapStopAdvertising(arguments);
}

void HandleGapSetModeWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGapSetMode(arguments);
}

void HandleGapSetBt5ModeWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGapSetBt5Mode(arguments);
}

void HandleGapConnOpenWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGapConnOpen(arguments);
}

void HandleGapSetAdvParamsWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGapSetAdvParams(arguments);
}

void HandleGapSetConnParamsWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGapSetConnParams(arguments);
}

void HandleGapUpdateConnParamsWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGapUpdateConnParams(arguments);
}

void HandleGattDiscoverPrimaryServicesWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGattDiscoverPrimaryServices(arguments);
}

void HandleGattDiscoverCharacteristicsWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGattDiscoverCharacteristics(arguments);
}

void HandleGattSetCharacteristicNotificationWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGattSetCharacteristicNotification(arguments);
}

void HandleGattWriteCharacteristicWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGattWriteCharacteristic(arguments);
}

void HandleGattCloseWrapper(sl_cli_command_arg_t * arguments)
{
    HandleGattClose(arguments);
}
}
