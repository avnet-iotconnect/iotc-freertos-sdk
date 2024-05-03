/*
 * iotconnect_config.h
 *
 *  Created on: Nov 3, 2023
 *      Author: mgilhespie
 */

#ifndef CONFIG_IOTCONNECT_CONFIG_H_
#define CONFIG_IOTCONNECT_CONFIG_H_

#include "message_buffer.h"
extern MessageBufferHandle_t iotcAppQueueTelemetry;
#define IOTC_APP_QUEUE_SIZE_TELEMETRY	(10)

/* @brief Enable discovery and sync instead of providing mqtt endpoint and telemetry cd settings
 */
//#define IOTCONFIG_USE_DISCOVERY_SYNC
#define IOTCONFIG_ENABLE_OTA



#endif /* CONFIG_IOTCONNECT_CONFIG_H_ */
