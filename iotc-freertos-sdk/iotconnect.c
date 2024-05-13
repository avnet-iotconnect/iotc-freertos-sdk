/*
 * iotconnect.c
 *
 *  Created on: Oct 23, 2023
 *      Author: mgilhespie
 */

/* Standard library includes */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "sys_evt.h"
#include "kvstore.h"
#include "hw_defs.h"
#include <string.h>

#include "lfs.h"
#include "lfs_port.h"

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
#include "iotcl.h"
#include "iotcl_c2d.h"
#include "iotcl_certs.h"
#include "iotcl_cfg.h"
#include "iotcl_log.h"
#include "iotcl_util.h"
#include "iotcl_certs.h"
#include <iotcl_dra_discovery.h>
#include <iotcl_dra_identity.h>
#include <iotcl_dra_url.h>

#include "iotconnect_config.h"
#include "iotc_https_client.h"


/* Constants */
#define HTTPS_PORT				443
#define DISCOVERY_SERVER_HOST	"awsdiscovery.iotconnect.io"

#define RESPONSE_BUFFER_SZ		4096
#define METHOD_BUFFER_SZ		256

/* Variables */
static char *response_buffer;
static IotConnectClientConfig config = { 0 };
static IotConnectDeviceClientConfig client_config;

/* Prototypes */
static int run_http_identity(const char *cpid, const char *env,
		const char *duid);
static int iotconnect_https_request(IotConnectHttpResponse *response,
		const char *host_name, const char *url);


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
int iotconnect_sdk_init(IotConnectCustomMQTTConfig *custom_mqtt_config) {
	int ret;
	int status;
    char cpid_buff[6];
	IotclClientConfig iotcl_cfg;

    IOTCL_INFO("iotconnect_sdk_init");

    if (config.cpid == NULL || config.env == NULL || config.duid == NULL) {
    	IOTCL_ERROR(0, "iotconnect_sdk_init failed, config uninitialized");
    	return -1;
    }

    strncpy(cpid_buff, config.cpid, 5);
    cpid_buff[5] = 0;
    IOTCL_INFO("IOTC: CPID: %s***************************", cpid_buff);
    IOTCL_INFO("IOTC: ENV :  %s\r\n", config.env);
    IOTCL_INFO("IOTC: DUID:  %s\r\n", config.duid);

	memset(&client_config, 0, sizeof(client_config));


	iotcl_init_client_config(&iotcl_cfg);
	iotcl_cfg.device.cpid = config.cpid;
	iotcl_cfg.device.duid = config.duid;
	iotcl_cfg.device.instance_type = IOTCL_DCT_CUSTOM;
	iotcl_cfg.mqtt_send_cb = iotc_device_client_mqtt_publish;
	iotcl_cfg.events.cmd_cb = config.cmd_cb;
	iotcl_cfg.events.ota_cb = config.ota_cb;

	IOTCL_INFO(" ***** MQTT send cb = %08x", (uintptr_t)iotcl_cfg.mqtt_send_cb);
	
	status = iotcl_init(&iotcl_cfg);

	if (status) {
		IOTCL_ERROR(status, "iotcl_failed to initialize");
		iotconnect_sdk_deinit();
		return status; // called function will print errors
	}

	if (custom_mqtt_config == NULL) {
		PkiObject_t https_ca_cert =
						PKI_OBJ_PEM((const unsigned char *)IOTCL_CERT_GODADDY_SECURE_SERVER_CERTIFICATE_G2, sizeof(IOTCL_CERT_GODADDY_SECURE_SERVER_CERTIFICATE_G2));

		iotconnect_https_init(https_ca_cert);

		if ((ret = run_http_identity(config.cpid, config.env, config.duid))
				!= 0) {
			IOTCL_ERROR(ret, "Failed to perform http identity");
			return -1;
		}

		IOTCL_INFO("IOTC: Discovery complete");

		client_config.host = iotcl_mqtt_get_config()->host;
		client_config.c2d_topic = iotcl_mqtt_get_config()->sub_c2d;

	} else {
		IOTCL_INFO("IOTC: Using custom config, skipping discovery");

		char *custom_c2d_topic = malloc(
				4 + strlen(iotcl_mqtt_get_config()->client_id) + 4 + 1);

		if (custom_c2d_topic == NULL) {
			IOTCL_ERROR(0, "Unable to allocate memory for custom c2d topic");
			return -1;
		}

		strcpy(custom_c2d_topic, "iot/");
		strcat(custom_c2d_topic, iotcl_mqtt_get_config()->client_id);
		strcat(custom_c2d_topic, "/cmd");

		client_config.host = custom_mqtt_config->host;
		client_config.c2d_topic = custom_c2d_topic;
	}

	client_config.duid = iotcl_mqtt_get_config()->client_id;
	client_config.auth = &config.auth_info;
	client_config.status_cb = NULL;  // TODO: on_iotconnect_status;

	IOTCL_INFO("IOTC: Initializing the mqtt connection");

	ret = iotc_device_client_connect(&client_config);

    if (ret) {
        IOTCL_ERROR(ret, "IOTC: Failed to connect to mqtt server");
    	return ret;
    }
    return ret;
}


/*
 *
 */
void iotconnect_sdk_deinit(void) {
	// TODO: perform any deinitialization
}


/* @brief	Send a discovery and identity HTTP Get request to populate config fields.
 *
 * SEE SOURCES    https://github.com/aws/aws-iot-device-sdk-embedded-C/blob/main/demos/http/http_demo_plaintext/http_demo_plaintext.c
 *
 * Or split into two functions as azure-rtos does
 */
static int run_http_identity(const char *cpid, const char *env,
		const char *duid) {
	IotclDraUrlContext discovery_url = { 0 };
	IotclDraUrlContext identity_url = { 0 };
	IotclClientConfig config;
	IotConnectHttpResponse http_response;

	IOTCL_INFO("IOTC: Performing discovery...");

	response_buffer = malloc(RESPONSE_BUFFER_SZ);

	if (response_buffer == NULL) {
		IOTCL_ERROR(0, "IOTC: Failed to allocate disconvery/sync response buffer");
		return -1;
	}

	iotcl_dra_discovery_init_url_aws(&discovery_url, cpid, env);

	// run HTTP GET with your http client with application/json content type
	iotconnect_https_request(&http_response, DISCOVERY_SERVER_HOST,
			iotcl_dra_url_get_url(&discovery_url));

	// parse the REST API base URL from discovery response
	iotcl_dra_discovery_parse(&identity_url, 0, http_response.data);

	// build  the actual identity API REST url on top of the base URL
	iotcl_dra_identity_build_url(&identity_url, duid);

	char *host_name = iotcl_dra_url_get_hostname(&identity_url);

	// run HTTP GET with your http client with application/json content type
	iotconnect_https_request(&http_response,
			iotcl_dra_url_get_hostname(&identity_url),
			iotcl_dra_url_get_url(&identity_url));

	// pass the body of the response to configure the MQTT library
	iotcl_dra_identity_configure_library_mqtt(http_response.data);

	// from here on you can call iotcl_mqtt_get_config() and use the iotcl mqtt functions

	iotcl_dra_url_deinit(&discovery_url);
	iotcl_dra_url_deinit(&identity_url);

	if (response_buffer != NULL) {
		free(response_buffer);
	}

	IOTCL_INFO("Printing config");

	iotcl_mqtt_print_config();

	return 0;
}


/*
 *
 */
static int iotconnect_https_request(IotConnectHttpResponse *response,
		const char *host_name, const char *url) {

	IOTCL_INFO("iotconnect_https_request\r\n");
	IOTCL_INFO("https url: %s\r\n", url);

	int32_t returnStatus;
	returnStatus = iotc_send_http_request(response, host_name,
			HTTPS_PORT, "GET", url, response_buffer, RESPONSE_BUFFER_SZ);

	IOTCL_INFO("HTTPS RESPONSE: %s\r\n", response_buffer);

	if (returnStatus != HTTPSuccess) {
		IOTCL_ERROR(returnStatus, "Failed the discovery HTTP Get request");
		return -1;
    }

	return 0;
}


