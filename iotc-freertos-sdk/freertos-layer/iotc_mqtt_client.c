/*
 * iotc_awsmqttcore_client.c
 *
 *  Created on: Oct 23, 2023
 *      Author: mgilhespie
 */

/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "mbedtls_transport.h"

/* MQTT includes */
#include "core_mqtt.h"
#include "core_mqtt_agent.h"
#include "mqtt_agent_task.h"
#include "subscription_manager.h"

/* IoT-Connect includes */
#include "iotconnect.h"
#include "iotcl.h"
#include "iotcl_c2d.h"
#include "iotcl_certs.h"
#include "iotcl_cfg.h"
#include "iotcl_log.h"
#include "iotcl_util.h"
#include <iotc_mqtt_client.h>
#include "sys_evt.h"

// @brief 	Defines the structure to use as the command callback context in this demo.
struct MQTTAgentCommandContext
{
    MQTTStatus_t xReturnStatus;
    TaskHandle_t xTaskToNotify;
    uint32_t ulNotificationValue;
};


// @brief 	The MQTT agent manages the MQTT contexts.  This set the handle to the context used by this demo.
extern MQTTAgentContext_t xGlobalMqttAgentContext;

// @beief Handle to MQTT agent
static MQTTAgentHandle_t xMQTTAgentHandle = NULL;

// @brief 	Handle to message queue of acknowledgements offloaded onto vMQTTSubscribeTask.
static QueueHandle_t mqtt_command_queue = NULL;


// Prototypes
static void mqtt_command_task(void *pvParameters);
static void publish_complete_callback(MQTTAgentCommandContext_t * pxCommandContext,
                                      MQTTAgentReturnInfo_t * pxReturnInfo);
static BaseType_t publish_and_wait_for_ack(MQTTAgentHandle_t xAgentHandle,
                                           const char * pcTopic,
                                           const void * pvPublishData,
                                           size_t xPublishDataLen);
static MQTTStatus_t subscribe_to_topic(MQTTQoS_t xQoS,
		const char *pcTopicFilter);
static void incoming_message_callback(void *pvIncomingPublishCallbackContext, MQTTPublishInfo_t *pxPublishInfo);


/* @brief	Initialize the MQTT client and associated tasks for publishing and receiving commands
 * *
 * TODO: This could be merged with the Common/app/mqtt/ore mqtt_agent_task.c code.  This may
 * avoid the need for creating the additional iotc_publish_events_task in this file that
 * handles the publishing of messages.
 */

int iotc_device_client_connect(IotConnectDeviceClientConfig *c) {
    BaseType_t xResult;
    MQTTStatus_t xMQTTStatus;

	configASSERT( xResult == MQTTSuccess );

	xResult = xTaskCreate(vMQTTAgentTask, "MQTTAgent", 4096, (void*) c, 10,
			NULL);
    
	if (xResult != pdTRUE) {
		IOTCL_ERROR(xResult, "Failed to create MQTT Agent task");
		return -1;
	}

	mqtt_command_queue = xQueueCreate(MQTT_COMMAND_QUEUE_LENGTH, sizeof(char*));
	if (mqtt_command_queue == NULL) {
		IOTCL_ERROR(0, "Failed to create Ack message queue");
		return -1;
	}

    vSleepUntilMQTTAgentReady();
    xMQTTAgentHandle = xGetMqttAgentHandle();
    configASSERT( xMQTTAgentHandle != NULL );
    vSleepUntilMQTTAgentConnected();


	xMQTTStatus = subscribe_to_topic(MQTTQoS1, c->c2d_topic);// Deliver at least once

	if( xMQTTStatus != MQTTSuccess ) {
		IOTCL_ERROR(xMQTTStatus, "Failed to subscribe to topic: %s.", c->c2d_topic);
		return -1;
	}

	xResult = xTaskCreate(mqtt_command_task, "mqtt_cmd", 2048, NULL, 9,
			NULL);

    if (xResult != pdTRUE ) {
		IOTCL_ERROR(xResult, "Failed to create iotc_pub_events_task");
    	return -1;
    }
    
	return 0;
}


/* @brief	Disconnect from the MQTT server
 *
 */
void iotc_device_client_disconnect(void)
{
	// TODO
}


/* @brief	Determine if the device is connected to the MQTT server
 *
 */
bool iotc_device_client_is_connected(void)
{
    if (xIsMqttAgentConnected() == pdTRUE ) {
    	return true;
    } else {
    	return false;
    }
}

/* @brief	Publish a message
 *
 * @param   topic, MQTT topic to publish to
 * @param	json_str, JSON formatted string to publish on this topic.
 *
 * This publishes a message and waits for an acknowledgement.  This can be
 * called to send telemetry from any task. It should not be called from
 * code running on the MQTT Agent Task such as the incoming_message_callback.
 * in order to avoid blocking the MQTT Agent Task and potentially causing a
 * deadlock.
 */
void iotc_device_client_mqtt_publish(const char *topic, const char *json_str)
{
	int len = strlen(json_str);
	int status = publish_and_wait_for_ack(xMQTTAgentHandle, topic, json_str,
			len);

	if (status != MQTTSuccess) {
		IOTCL_ERROR(status, "Publishing a message to %s failed\r\n", topic);
	}
}


/*-----------------------------------------------------------*/


/* @brief 	Task that offloads publishing to the ACK_PUBLISH_TOPIC_FORMAT topic
 *
 * @param 	pvParameters, The parameters passed to the task.
 */
static void mqtt_command_task(void *pvParameters)
{
    ( void ) pvParameters;
    BaseType_t xStatus;
	char *message;
    int status;
    
    while (1) {
		xStatus = xQueueReceive(mqtt_command_queue, &message, portMAX_DELAY);
        if (xStatus != pdPASS) {
            IOTCL_ERROR(xStatus, "mqtt command task recv error");
            break;
        }

		if (message) {
			if ((status = iotcl_c2d_process_event(message)) != IOTCL_SUCCESS) {
				IOTCL_ERROR(status, "Failed to process c2d message");
            }

			free(message);
        }
    }

    vTaskDelete( NULL );
}


/* @brief	Completion routine when a message has been published.
 *
 */
static void publish_complete_callback( MQTTAgentCommandContext_t * pxCommandContext,
                                       MQTTAgentReturnInfo_t * pxReturnInfo )
{
    TaskHandle_t xTaskHandle = ( TaskHandle_t ) pxCommandContext;

    configASSERT( pxReturnInfo != NULL );

    uint32_t ulNotifyValue = pxReturnInfo->returnCode;

    if( xTaskHandle != NULL ) {
        /* Send the context's ulNotificationValue as the notification value so
         * the receiving task can check the value it set in the context matches
         * the value it receives in the notification. */
        ( void ) xTaskNotifyIndexed( xTaskHandle,
                                     MQTT_NOTIFY_IDX,
                                     ulNotifyValue,
                                     eSetValueWithOverwrite );
    }
}


/* @brief	Publish to an MQTT topic and wait for an acknowledgement
 *
 */
static BaseType_t publish_and_wait_for_ack( MQTTAgentHandle_t xAgentHandle,
                                           const char * pcTopic,
                                           const void * pvPublishData,
                                           size_t xPublishDataLen )
{
    MQTTStatus_t xStatus;
    size_t uxTopicLen = 0;

    configASSERT( pcTopic != NULL );
    configASSERT( pvPublishData != NULL );
    configASSERT( xPublishDataLen > 0 );

    uxTopicLen = strnlen( pcTopic, UINT16_MAX );

    MQTTPublishInfo_t xPublishInfo = {
        .qos             = MQTT_PUBLISH_QOS,
        .retain          = 0,
        .dup             = 0,
        .pTopicName      = pcTopic,
        .topicNameLength = ( uint16_t ) uxTopicLen,
        .pPayload        = pvPublishData,
        .payloadLength   = xPublishDataLen
    };

    MQTTAgentCommandInfo_t xCommandParams = {
        .blockTimeMs                 = MQTT_PUBLISH_BLOCK_TIME_MS,
        .cmdCompleteCallback         = publish_complete_callback,
        .pCmdCompleteCallbackContext = ( void * ) xTaskGetCurrentTaskHandle(),
    };

    if (xPublishInfo.qos > MQTTQoS0) {
        xCommandParams.pCmdCompleteCallbackContext = ( void * ) xTaskGetCurrentTaskHandle();
    }

    /* Clear the notification index */
    xTaskNotifyStateClearIndexed( NULL, MQTT_NOTIFY_IDX );

    xStatus = MQTTAgent_Publish( xAgentHandle, &xPublishInfo, &xCommandParams );

    if (xStatus == MQTTSuccess) {
        uint32_t ulNotifyValue = 0;
        BaseType_t xResult = pdFALSE;

        xResult = xTaskNotifyWaitIndexed( MQTT_NOTIFY_IDX,
                                          0xFFFFFFFF,
                                          0xFFFFFFFF,
                                          &ulNotifyValue,
                                          pdMS_TO_TICKS( MQTT_PUBLISH_NOTIFICATION_WAIT_MS ) );

        if( xResult ) {
            xStatus = ( MQTTStatus_t ) ulNotifyValue;
            if( xStatus != MQTTSuccess ) {
                IOTCL_ERROR(xStatus, "MQTT Agent returned error during publish operation");
                xResult = pdFALSE;
            }
        } else {
			IOTCL_ERROR(xResult,
					"Timed out while waiting for publish ACK or Sent event. xTimeout = %d",
            							pdMS_TO_TICKS( MQTT_PUBLISH_NOTIFICATION_WAIT_MS ) );
            xResult = pdFALSE;
        }
    } else {
        IOTCL_ERROR(xStatus, "MQTTAgent_Publish failed");
    }

	return xStatus;
}


/*
 *
 */
static MQTTStatus_t subscribe_to_topic(MQTTQoS_t xQoS,
		const char *pcTopicFilter)
{
    MQTTStatus_t xMQTTStatus;
    int retries = 0;
    
    do
    {
        xMQTTStatus = MqttAgent_SubscribeSync( xMQTTAgentHandle,
                                               pcTopicFilter,
                                               xQoS,
                                               incoming_message_callback,
                                               NULL );
        
        retries++;
    } while( xMQTTStatus != MQTTSuccess && retries < 20);

    if( xMQTTStatus != MQTTSuccess ) {
        IOTCL_ERROR(xMQTTStatus, "Failed to subscribe to topic");
    } else {
        IOTCL_INFO("Subscribed to topic %s", pcTopicFilter);
    }

    return xMQTTStatus;
}


/* @brief	 Callback on receipt of a cloud-to-device message
 *
 * Passed into MQTTAgent_Subscribe() as the callback to execute when there is an
 * incoming publish on the topic being subscribed to.
 *
 * This callback runs on the context of the MQTT Agent Task.  This should be kept as
 * short as possible and not perform any blocking operations otherwise there is a
 * potential for deadlock or the connection dropping.
 *
 * Here we offload the processing of commands onto the mqtt_command_task by placing
 * the received JSON message into a queue that the mqtt_command_task reads from.
 *
 * @param[in] pvIncomingPublishCallbackContext Context of the initial command.
 * @param[in] pxPublishInfo Deserialized publish.
 */
static void incoming_message_callback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo )
{
    ( void ) pvIncomingPublishCallbackContext;
	int status;
	char *buf;

	buf = malloc(MQTT_PAYLOAD_BUFFER_LENGTH);

	if (!buf) {
		IOTCL_ERROR(0, "failed to allocate message buf");
		return;
    }

	if (pxPublishInfo->payloadLength < MQTT_PAYLOAD_BUFFER_LENGTH) {
		memcpy(buf, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
		buf[pxPublishInfo->payloadLength] = '\0';
	} else {
		memcpy(buf, pxPublishInfo->pPayload, MQTT_PAYLOAD_BUFFER_LENGTH);
		buf[MQTT_PAYLOAD_BUFFER_LENGTH - 1] = '\0';
	}

	status = xQueueSendToBack(mqtt_command_queue, &buf,
			MQTT_COMMAND_QUEUE_TIMEOUT_MS);

	if (status != pdTRUE) {
		free(buf);
    }
}

