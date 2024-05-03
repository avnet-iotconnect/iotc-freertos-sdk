/*
 * iotconnect.c
 *
 *  Created on: Oct 23, 2023
 *      Author: mgilhespie
 */

#include "logging_levels.h"
#define LOG_LEVEL	LOG_INFO
#include "logging.h"

/* Standard library includes */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "sys_evt.h"
//#include "stm32u5xx.h"
#include "kvstore.h"
#include "hw_defs.h"
#include <string.h>

#include "lfs.h"
#include "lfs_port.h"
//TODO:this won't work forever
#include "stm32h5xx_ll_rng.h"

#include "core_http_client.h"
#include "transport_interface.h"

/* Transport interface implementation include header for TLS. */
#include "mbedtls_transport.h"

/* MQTT library includes. */
#include "core_mqtt.h"

/* MQTT agent include. */
#include "core_mqtt_agent.h"
#include "mqtt_agent_task.h"

#include <iotc_mqtt_client.h>

//Iotconnect
#include "iotconnect.h"
#include "iotconnect_lib.h"
#include "iotconnect_telemetry.h"
#include "iotconnect_event.h"
#include "iotconnect_discovery.h"
#include "iotconnect_certs.h"
#include "iotconnect_config.h"
#include "iotc_https_client.h"

/* Constants */
#define HTTPS_PORT				443
#define DISCOVERY_SERVER_HOST	"awsdiscovery.iotconnect.io"

#define RESPONSE_BUFFER_SZ		4096
#define METHOD_BUFFER_SZ		256

/* Variables */

//static char response_buffer[4096] = {0};		// FIXME: HTTPS request/response buffer, dynamically allocate.
												// FIXME: dynamically allocate and free after use
//static char discovery_method_path[256];			// FIXME: dynamically allocate and free after use size
//static char identity_method_path[256];			// FIXME: dynamically allocate and free after use
//#else

#if defined(IOTCONFIG_USE_DISCOVERY_SYNC)
static int alloc_discovery_sync_http_buffers(void);
static void free_discovery_sync_http_buffers(void);
static char *response_buffer;
static char *discovery_method_path;			// FIXME: Can we use a single buffer (for all 3 items)
static char *identity_method_path;

#endif

static IotConnectClientConfig config = { "POC",
	"7FF86E8728645E9B89F7B07977E4B15",
	"py",
	0,
	0,
	0,
	0,
	0
};
static IotclConfig lib_config = { 0 };
static IotclSyncResult last_sync_result = IOTCL_SR_UNKNOWN_DEVICE_STATUS;
static IotConnectAWSMQTTConfig awsmqtt_config;


// Prototypes
static IotclDiscoveryResponse *run_http_discovery(const char *cpid, const char *env);
static IotclSyncResponse *run_http_sync(const char *host, const char *disc_method_path, const char *device_id);



/* @brief   Pre-initialization of SDK's configuration and return pointer to it.
 *
 */
IotConnectClientConfig* iotconnect_sdk_init_and_get_config(void) {
    memset(&config, 0, sizeof(config));
    return &config;
}



/* @brief	This the Initialization os IoTConnect SDK
 *
 */
int iotconnect_sdk_init(IotConnectAwsrtosConfig *awsrtos_config) {
	int ret;
	IotclDiscoveryResponse *discovery_response = NULL;
	IotclSyncResponse *sync_response = NULL;
    char cpid_buff[6];

    LogInfo("iotconnect_sdk_init");

    if (config.cpid == NULL || config.env == NULL || config.duid == NULL) {
    	LogError("iotconnect_sdk_init: configuration uninitialized");
    	return -1;
    }

    strncpy(cpid_buff, config.cpid, 5);
    cpid_buff[5] = 0;
    LogInfo("IOTC: CPID: %s***************************\r\n", cpid_buff);
    LogInfo("IOTC: ENV :  %s\r\n", config.env);
    LogInfo("IOTC: DUID:  %s\r\n", config.duid);

    memset(&awsmqtt_config, 0, sizeof(awsmqtt_config));

    last_sync_result = IOTCL_SR_UNKNOWN_DEVICE_STATUS;

    lib_config.device.cpid = config.cpid;
    lib_config.device.env = config.env;
    lib_config.device.duid = config.duid;

#if defined(IOTCONFIG_USE_DISCOVERY_SYNC)
    PkiObject_t https_ca_cert = PKI_OBJ_PEM((const unsigned char *)GODADDY_ROOT_CERTIFICATE_AUTHORITY, sizeof(GODADDY_ROOT_CERTIFICATE_AUTHORITY));
    PkiObject_t https_ca_cert_g2 = PKI_OBJ_PEM((const unsigned char *)GODADDY_ROOT_CERTIFICATE_AUTHORITY_G2, sizeof(GODADDY_ROOT_CERTIFICATE_AUTHORITY_G2));
    //iotconnect_https_init(https_ca_cert);
    iotconnect_https_init(https_ca_cert_g2);

    LogInfo("IOTC: Performing discovery...\r\n");

    if (alloc_discovery_sync_http_buffers() != 0) {
    	LogError("Failed to allocate buffers for discovery/sync");
    	return -1;
    }

    discovery_response = run_http_discovery(config.cpid, config.env);

    if (NULL == discovery_response) {
        LogError("IOTC: discovery failed\r\n");
        free_discovery_sync_http_buffers();
        return -1;
    }

    LogInfo("IOTC: Discovery response parsing successful. Performing sync...\r\n");

    sync_response = run_http_sync(discovery_response->host, discovery_response->path, config.duid);
    if (NULL == sync_response) {
        LogError("IOTC: sync failed\r\n");
        free_discovery_sync_http_buffers();
        return -2;
    }

    free_discovery_sync_http_buffers();

    LogInfo("IOTC: Sync response parsing successful.\r\n");

    awsmqtt_config.device_name = config.duid;
    awsmqtt_config.host = sync_response->broker.host;
    awsmqtt_config.port = sync_response->broker.port;
    awsmqtt_config.auth = &config.auth_info;
    // awsmqtt_config.status_cb = on_iotconnect_status;

#else
    LogInfo("IOTC: setting cd, duid and host from supplied config");

    awsmqtt_config.device_name = config.duid;
    awsmqtt_config.host = awsrtos_config->host;
    awsmqtt_config.port = 8883;
    awsmqtt_config.auth = &config.auth_info;
    // awsmqtt_config.status_cb = on_iotconnect_status;
#endif

    lib_config.event_functions.ota_cb = config.ota_cb;
    lib_config.event_functions.cmd_cb = config.cmd_cb;
    lib_config.event_functions.msg_cb = config.msg_cb;

    // Initialize the iotc-c-lib for awsformat2.1
    if (!iotcl_init_v2(&lib_config)) {
        LogError(("IOTC: Failed to initialize the IoTConnect C Lib"));
        return -1;
    }

    LogInfo(("IOTC: Initializing the mqtt connection"));

    ret = awsmqtt_client_init(&awsmqtt_config, awsrtos_config);
    if (ret) {
        LogError(("IOTC: Failed to connect to mqtt server"));
    	return ret;
    }
    return ret;
}


/* @brief	Publish a message on the defined publish topic
 *
 * @param	data, json message to be published
 */
void iotconnect_sdk_send_packet(const char *data) {
    if (awsmqtt_send_message(data) != 0) {
        LogError(("IOTC: Failed to send message %s\r\n", data));
    }
}


#if defined(IOTCONFIG_USE_DISCOVERY_SYNC)

int alloc_discovery_sync_http_buffers(void)
{
	response_buffer = malloc(RESPONSE_BUFFER_SZ);
	if (response_buffer == NULL) {
		LogError("IOTC: Failed to allocate disconvery/sync response buffer");
		return -1;
	}

	discovery_method_path = malloc(METHOD_BUFFER_SZ);
	if (discovery_method_path == NULL) {
		LogError("IOTC: Failed to allocate disconvery methed path");
		free(response_buffer);
		response_buffer = NULL;
		return -1;
	}

	identity_method_path = malloc(METHOD_BUFFER_SZ);
	if (identity_method_path == NULL) {
		LogError("IOTC: Failed to allocate sync methed path");
		free(discovery_method_path);
		free(response_buffer);
		discovery_method_path = NULL;
		response_buffer = NULL;
		return -1;
	}

	return 0;
}


void free_discovery_sync_http_buffers(void)
{
	if (response_buffer != NULL) {
		free(response_buffer);
	}

	if (discovery_method_path == NULL) {
		free(discovery_method_path);
	}

	if (identity_method_path == NULL) {
		free(identity_method_path);
	}
}


/* @brief	Send a discovery and identity HTTP Get request to populate config fields.
 *
 * SEE SOURCES    https://github.com/aws/aws-iot-device-sdk-embedded-C/blob/main/demos/http/http_demo_plaintext/http_demo_plaintext.c
 *
 * Or split into two functions as azure-rtos does
 */
static IotclDiscoveryResponse *run_http_discovery(const char *cpid, const char *env)
{
	HTTPStatus_t returnStatus;
	char *response_body = NULL;
	IotConnectHttpResponse http_response;
	IotclDiscoveryResponse *response;

	discovery_method_path[0] = '\0';
    snprintf (discovery_method_path, METHOD_BUFFER_SZ, "/api/v2.1/dsdk/cpId/%s/env/%s", cpid, env);

	returnStatus = iotc_send_http_request(&http_response, DISCOVERY_SERVER_HOST, HTTPS_PORT,
			                        "GET", discovery_method_path,
									response_buffer, RESPONSE_BUFFER_SZ);

    if (returnStatus != HTTPSuccess) {
    	LogError(("Failed the discovery HTTP Get request"));
		return NULL;
    }

    LogDebug("sync_response:%s\n", http_response.data);

    if (http_response.data == NULL) {
        LogError("Unable to parse HTTP response,", &response);
        return NULL;
    }
    char *json_start = strstr(http_response.data, "{");
    if (NULL == json_start) {
        LogError("No json response from discovery server.", &response);
        return NULL;
    }

    response = iotcl_discovery_parse_discovery_response(http_response.data);

    if (response == NULL) {
    	LogError(("Failed to parse discovery response"));
    	return NULL;
    }

    if (response->host == NULL || response->path == NULL) {
    	LogError(("Discovery response did not return host or method path"));
    	iotcl_discovery_free_discovery_response(response);
    	return NULL;
    }

    return response;
}


/* @brief	Send a discovery and identity HTTP Get request to populate config fields.
 *
 * SEE SOURCES    https://github.com/aws/aws-iot-device-sdk-embedded-C/blob/main/demos/http/http_demo_plaintext/http_demo_plaintext.c
 *
 * Or split into two functions as azure-rtos does
 */
static IotclSyncResponse *run_http_sync(const char *host, const char *disc_method_path, const char *device_id)
{
	HTTPStatus_t returnStatus = HTTPSuccess;
	char *response_body = NULL;
	IotConnectHttpResponse http_response;
	IotclSyncResponse *response;

    snprintf (identity_method_path, METHOD_BUFFER_SZ, "%s/uid/%s", disc_method_path, device_id);

	returnStatus = iotc_send_http_request(&http_response, host, HTTPS_PORT,
			                        "GET", identity_method_path,
									response_buffer, RESPONSE_BUFFER_SZ);


    if (returnStatus != HTTPSuccess) {
    	LogError(("Failed the discovery HTTP Get request"));
		return NULL;
    }

    LogDebug("sync_response:%s\n", http_response.data);

	response = iotcl_discovery_parse_sync_response(http_response.data);

	if (response == NULL) {
		LogError(("Failed to parse sync response"));
		return NULL;
	}

	if (response->broker.host == NULL) {
		LogError ("sync response no broker.host");
		iotcl_discovery_free_sync_response(response);
		return NULL;
	}

	if (response->broker.port == 0) {
		LogError("sync response no broker port");
		iotcl_discovery_free_sync_response(response);
		return NULL;
	}

	if (response->cd == NULL) {
		LogError ("sync response no telemetry cd");
		iotcl_discovery_free_sync_response(response);
		return NULL;
	}

	// TODO: Check pub/sub topic strings in response

	return response;
}

#endif
