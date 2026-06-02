/**
 * @file MatterAwsDoorLockRemote.h
 * @brief Hook for applying Door Lock MQTT commands on Silabs lock apps.
 *******************************************************************************
 * # License
 * <b>Copyright 2023 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 */

#pragma once

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <lib/core/DataModelTypes.h>

/**
 * @brief Apply a proprietary-remote lock/unlock request from AWS MQTT.
 *
 * Silabs lock-app provides a strong implementation that routes through AppTask
 * (`EnqueueLockRequest`). Other apps use the weak default (cluster state only).
 *
 * @return true if the request was accepted for processing.
 */
bool MatterAwsApplyDoorLockRemoteCommand(chip::EndpointId endpointId,
                                       chip::app::Clusters::DoorLock::DlLockState lockState);
