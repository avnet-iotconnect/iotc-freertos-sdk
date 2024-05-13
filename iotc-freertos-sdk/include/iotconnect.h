//
// Copyright: Avnet, Softweb Inc. 2020
// Modified by Marven Gilhespie <mgilhespie@witekio.com> on 24/10/23
//

#ifndef IOTCONNECT_H
#define IOTCONNECT_H

#include <stddef.h>
#include "iotcl.h"
#include "iotcl_c2d.h"
#include "iotcl_certs.h"
#include "iotcl_cfg.h"
#include "iotcl_log.h"
#include "iotcl_util.h"
#include "PkiObject.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UNDEFINED,
    MQTT_CONNECTED,
    MQTT_DISCONNECTED,
    // TODO: Sync statuses etc.
} IotConnectConnectionStatus;

typedef enum {
    IOTC_KEY,		// Symmetric key
	IOTC_X509, 		// Private key and ceritificate
} IotConnectAuthType;

typedef void (*IotConnectStatusCallback)(IotConnectConnectionStatus data);

typedef struct {
    IotConnectAuthType type;
    PkiObject_t mqtt_root_ca;

    union { // union because we may support different types of auth
    	struct
    	{
    		PkiObject_t device_cert;
    		PkiObject_t device_key;
    	} cert_info;
    } data;
} IotConnectAuth;

typedef struct {
    char *env;    // Environment name. Contact your representative for details.
    char *cpid;   // Settings -> Company Profile.
    char *duid;   // Name of the device.
    IotConnectAuth auth_info;
    IotclOtaCallback ota_cb; // callback for OTA events.
    IotclCommandCallback cmd_cb; // callback for command events.
    IotConnectStatusCallback status_cb; // callback for connection status
} IotConnectClientConfig;

typedef struct {
	char *host;
} IotConnectCustomMQTTConfig;


IotConnectClientConfig *iotconnect_sdk_init_and_get_config();
int iotconnect_sdk_init(IotConnectCustomMQTTConfig *custom_mqtt_config);
bool iotconnect_sdk_is_connected(void);
void iotconnect_sdk_send_packet(const char *data);

/* Note: Neither IotConnectSdk_receive nor iotconnect_sdk_poll are used by this
 * STM U5 AWS implementation.  An internal thread handles the receipt of messages
 * on the subscribed topic
 */

// Receive loop hook forever-blocking for for C2D messages.
// Either call this function, or IoTConnectSdk_Poll()
void iotconnect_sdk_receive(void);

// Receive poll hook for for C2D messages.
// Either call this function, or IotConnectSdk_Receive()
// Set wait_time to a multiple of NX_IP_PERIODIC_RATE
void iotconnect_sdk_poll(int wait_time_ms);

void iotconnect_sdk_disconnect();

int iotc_ota_fw_download(const char* host, const char* path);
int iotc_ota_fw_apply(void);


#ifdef __cplusplus
}
#endif

#endif
