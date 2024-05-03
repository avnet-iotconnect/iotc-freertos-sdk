/*
 * iotc_device_client.h
 *
 *  Created on: Oct 23, 2023
 *      Author: mgilhespie
 */

#ifndef IOTC_AWSRTOS_SDK_PLACEHOLDER_INCLUDE_IOTC_AWSMQTT_CLIENT_H_
#define IOTC_AWSRTOS_SDK_PLACEHOLDER_INCLUDE_IOTC_AWSMQTT_CLIENT_H_

#include "iotconnect.h"

// @brief 	Format of topic string used to subscribe to incoming messages for this device
#define SUBSCRIBE_TOPIC_FORMAT   "iot/%s/cmd"

// @brief 	Format of topic string used to publish events (e.g. telemetry and acknowledgements for this device
//#define PUBLISH_TOPIC_FORMAT	"devices/%s/messages/events/"

#define PUBLISH_TOPIC_FORMAT	"$aws/rules/msg_d2c_rpt/%s/2.1/0"
//#define PUBLISH_TOPIC_FORMAT	"$aws/things/%s/shadow/name/setting_info/report"

// @brief 	Queue size of acknowledgements offloaded to the vMQTTSubscribeTask
#define ACK_MSG_Q_SIZE	5

// @brief 	Length of buffer to hold subscribe topic string containing the device id (thing_name)
#define MQTT_SUBSCRIBE_TOPIC_STR_LEN           	( 256 )

// @brief 	Size of statically allocated buffers for holding payloads.
#define confgPAYLOAD_BUFFER_LENGTH           	( 1024 )

// @brief	Size of statically allocated buffers for holding topic names and payloads.
#define MQTT_PUBLISH_MAX_LEN                 ( 1024 )
#define MQTT_PUBLISH_PERIOD_MS               ( 3000 )
#define MQTT_PUBLISH_TOPIC_STR_LEN           ( 256 )
#define MQTT_PUBLISH_BLOCK_TIME_MS           ( 200 )
#define MQTT_PUBLISH_NOTIFICATION_WAIT_MS    ( 1000 )
#define MQTT_NOTIFY_IDX                      ( 1 )
#define MQTT_PUBLISH_QOS                     ( MQTTQoS0 )

typedef void (*IotConnectC2dCallback)(char* message, size_t message_len);

typedef struct {
    char *host;    			// Host to connect the client to
    int port;				// MQTT Port to connect to
    char *device_name;   	// Name of the device
	char *sub_topic;
    char *pub_topic;
    IotConnectAuth *auth; 				// Pointer to IoTConnect auth configuration
    IotConnectC2dCallback c2d_msg_cb; 	// callback for inbound messages
    IotConnectStatusCallback status_cb; // callback for connection status
} IotConnectAWSMQTTConfig;


int awsmqtt_client_init(IotConnectAWSMQTTConfig *c, IotConnectAwsrtosConfig* awsrtos_config);
void awsmqtt_client_disconnect(void);
bool awsmqtt_client_is_connected(void);
int awsmqtt_send_message(const char *message);	// send a null terminated string to MQTT host

MQTTStatus_t vSetMQTTConfig( const char *host,
						  int port,
						  const char *device_id,
						  const char *sub_topic,
						  const char *pub_topic,
						  PkiObject_t root_ca_cert,
						  PkiObject_t device_cert,
						  PkiObject_t device_key);



/**
Receive message(s) from IoTHub when a message is received, status_cb is called.

loop_forever if you wish to call this function from own thread.
wait_time will be ignored in this case and set NX_WAIT_FOREVER.

If calling from a single thread (loop_forever = false)
that will be sending and receiving set wait time to the desired value as a multiple of NX_IP_PERIODIC_RATE.
 *
 */
//UINT iothub_c2d_receive(bool loop_forever, ULONG wait_ticks);



#endif /* IOTC_AWSRTOS_SDK_PLACEHOLDER_INCLUDE_IOTC_AWSMQTT_CLIENT_H_ */
