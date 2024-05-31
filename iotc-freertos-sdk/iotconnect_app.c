//
// Copyright: Avnet 2023
// Created by Marven Gilhespie
//

#include "iotconnect_app.h"

#include "logging_levels.h"
/* define LOG_LEVEL here if you want to modify the logging level from the default */
#define LOG_LEVEL    LOG_INFO
#include "logging.h"

/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "message_buffer.h"

#include "kvstore.h"
#include "mbedtls_transport.h"

#include "sys_evt.h"
#include "ota_pal.h"

/* MQTT library includes. */
#include "core_mqtt.h"

/* MQTT agent include. */
#include "core_mqtt_agent.h"
#include "mqtt_agent_task.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

//Iotconnect
#include "iotconnect.h"
#include "iotcl.h"
#include "iotcl_c2d.h"
#include "iotcl_certs.h"
#include "iotcl_cfg.h"
#include "iotcl_log.h"
#include "iotcl_telemetry.h"
#include "iotcl_util.h"

#include <iotconnect_config.h>


// @brief	IOTConnect configuration defined by application
#if !defined(IOTCONFIG_USE_DISCOVERY_SYNC)
static IotConnectCustomMQTTConfig custom_mqtt_config;
#endif

static bool is_downloading = false;

MessageBufferHandle_t iotcAppQueueTelemetry = NULL;

// Prototypes
static BaseType_t init_sensors( void );
static void on_command(IotclC2dEventData data);
static void on_ota(IotclC2dEventData data);
static bool is_ota_agent_file_initialized(void);
static int split_url(const char *url, char **host_name, char**resource);
static bool is_app_version_same_as_ota(const char *version);
static bool app_needs_ota_update(const char *version);
static int start_ota(char *url);


/* @brief	Main IoT-Connect application task
 *
 * @param	pvParameters, argument passed by xTaskCreate
 *
 * This is started by the initialization code in app_main.c which first performs board and
 * networking initialization
 */
void iotconnect_app( void * )
{
    LogInfo( " ***** STARTING APP VERSION %s *****", APP_VERSION );

    // Get some settings from non-volatile storage.  These can be set on the command line
    // using the conf command.

    char *device_id = KVStore_getStringHeap(CS_CORE_THING_NAME, NULL);   // Device ID
    char *platform = KVStore_getStringHeap(CS_IOTC_PLATFORM, NULL);
    char *cpid = KVStore_getStringHeap(CS_IOTC_CPID, NULL);
    char *iotc_env = KVStore_getStringHeap(CS_IOTC_ENV, NULL);

    if (platform == NULL || device_id == NULL || cpid == NULL || iotc_env == NULL) {
    	LogError("IOTC configuration, platform, thing_name, cpid or env are not set\n");
		vTaskDelete(NULL);
    }

    iotcAppQueueTelemetry = xMessageBufferCreate(IOTC_APP_QUEUE_SIZE_TELEMETRY * sizeof(void *));
	if (iotcAppQueueTelemetry == NULL) {
		LogError("Failed to create iotcAppQueueTelemetry");
		vTaskDelete( NULL );
	}

    // IoT-Connect configuration setup
    ( void ) xEventGroupWaitBits( xSystemEvents,
                                  EVT_MASK_NET_CONNECTED,
                                  0x00,
                                  pdTRUE,
                                  portMAX_DELAY );

    IotConnectClientConfig *config = iotconnect_sdk_init_and_get_config();
    config->cpid = cpid;
    config->env = iotc_env;
    config->duid = device_id;
    config->cmd_cb = on_command;

#ifdef IOTCONFIG_ENABLE_OTA
    config->ota_cb = on_ota;
#else
    config->ota_cb = NULL;
#endif

    config->status_cb = NULL;
    config->auth_info.type = IOTC_X509;

    if (strcmp(platform, "aws") == 0) {
        config->connection_type = IOTC_CT_AWS;
    } else if (strcmp(platform, "azure") == 0) {
        config->connection_type = IOTC_CT_AZURE;
    } else {
        config->connection_type = IOTC_CT_UNDEFINED;
    }

    config->auth_info.mqtt_root_ca               = xPkiObjectFromLabel( TLS_MQTT_ROOT_CA_CERT_LABEL );
    config->auth_info.data.cert_info.device_cert = xPkiObjectFromLabel( TLS_CERT_LABEL );
    config->auth_info.data.cert_info.device_key  = xPkiObjectFromLabel( TLS_KEY_PRV_LABEL );;

#if defined(IOTCONFIG_USE_DISCOVERY_SYNC)
    // Get MQTT configuration from discovery and sync
    iotconnect_sdk_init(NULL);
#else
    // Not using Discovery and Sync so some additional settings, are obtained from the CLI,
    char *mqtt_endpoint_url = "a3etk4e19usyja-ats.iot.us-east-1.amazonaws.com";//KVStore_getStringHeap(CS_CORE_MQTT_ENDPOINT, NULL);

    if (mqtt_endpoint_url == NULL) {
    	LogError ("IOTC configuration, mqtt_endpoint not set");
    	vTaskDelete( NULL );
    }

    custom_mqtt_config.host = mqtt_endpoint_url;
    iotconnect_sdk_init(&custom_mqtt_config);
#endif

    while (1) {
        size_t n;
#define IOTC_TELEMETRY_MSG_SIZ (128)
        void *telemetryData[IOTC_TELEMETRY_MSG_SIZ];

        n = xMessageBufferReceive(iotcAppQueueTelemetry, &telemetryData, IOTC_TELEMETRY_MSG_SIZ, 0xffffffffUL);
        if (n == 0) {
			IOTCL_ERROR(n, "[%s] Q recv error \r\n", __func__);
            break;
        }

        iotcApp_create_and_send_telemetry_json(&telemetryData, n);
        //vTaskDelay( pdMS_TO_TICKS( MQTT_PUBLISH_PERIOD_MS ) );
    }
}

typedef struct EXAMPLE_IOTC_TELEMETRY {
	double doubleValue;
	bool boolValue;
	const char *strValue;
}exampleIotcTelemetry_t;

/* @brief 	Create JSON message containing telemetry data to publish
 *
 */
__weak void iotcApp_create_and_send_telemetry_json(
		const void *pToTelemetryStruct, size_t siz) {

    const struct EXAMPLE_IOTC_TELEMETRY * p = NULL;
    IotclMessageHandle msg = iotcl_telemetry_create();

    if(siz != sizeof(const struct EXAMPLE_IOTC_TELEMETRY)) {
        IOTCL_ERROR(siz, "Expected telemetry size does not match");
        return;
    }

    // Optional. The first time you create a data poiF\Z\nt, the current timestamp will be automatically added
    // TelemetryAddWith* calls are only required if sending multiple data points in one packet.
	// FIXME: new iotc-c-lib has new API for adding timestamps
	// iotcl_telemetry_add_with_iso_time(msg, NULL);

    iotcl_telemetry_set_number(msg, "double_value", p->doubleValue);
    iotcl_telemetry_set_bool(msg, "bool_value", p->boolValue);
    iotcl_telemetry_set_string(msg, "string_value", p->strValue);

    iotcl_telemetry_set_string(msg, "version", APP_VERSION);

    iotcl_mqtt_send_telemetry(msg, true);
    iotcl_telemetry_destroy(msg);
}

__weak int iotc_process_cmd_str(IotclC2dEventData data, char* command){
    LogInfo("Received command: %s", command);

    if(NULL != strcasestr(command, IOTC_CMD_PING)){
        LogInfo("Ping Command Received!\n");
#ifdef IOTC_USE_LED
    } else if(NULL != strcasestr(command, IOTC_CMD_LED_RED)){
        if (NULL != strcasestr(command, "on")) {
            LogInfo("led-red on");
            //BSP_LED_On(LED_RED);
            set_led_red(true);
        } else if (NULL != strcasestr(command, "off")) {
            LogInfo("led-red off");
            set_led_red(false);
        } else {
            LogWarn("Invalid led-red command received: %s", command);
        }
        // TODO: command_status(data, true, command, "OK");
    } else if(NULL != strcasestr(command, IOTC_CMD_LED_GREEN)) {
        if (NULL != strcasestr(command, "on")) {
            LogInfo("led-green on");
            set_led_green(true);
        } else if (NULL != strcasestr(command, "off")) {
            LogInfo("led-green off");
            set_led_green(false);
        } else {
            LogWarn("Invalid led-green command received: %s", command);
        }
        // TODO: command_status(data, true, command, "OK");
    } else if(NULL != strcasestr(command, IOTC_CMD_LED_FREQ)) {
        // Get the int following the command
        int led_freq = atoi(command + strlen(IOTC_CMD_LED_FREQ) + 1);
        if (0 != led_freq){
            set_led_freq(led_freq);
        }
#endif // IOTC_USE_LED
    } else {
        LogInfo("Command not recognized: %s", command);
        // TODO: command_status(data, false, command, "Not implemented");
    }
    return 0;
}

/* @brief	Callback when a a cloud-to-device command is received on the subscribed MQTT topic
 */
static void on_command(IotclC2dEventData data) {
    const char *command = iotcl_c2d_get_command(data);
    const char *ack_id = iotcl_c2d_get_ack_id(data);
    int err;

    if (command) {
        IOTCL_INFO("Command %s received with %s ACK ID\n", command,
                        ack_id ? ack_id : "no");

        err = iotc_process_cmd_str(data, command);

        if(ack_id) {
			if(!err)
				iotcl_mqtt_send_cmd_ack(ack_id, IOTCL_C2D_EVT_CMD_FAILED,
						"Command error");
			else
				iotcl_mqtt_send_cmd_ack(ack_id, IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK,
						"Command OK");
        }
    } else {
        IOTCL_ERROR(0, "No command, internal error");
        // could be a command without acknowledgement, so ackID can be null
        if (ack_id) {
            iotcl_mqtt_send_cmd_ack(ack_id, IOTCL_C2D_EVT_CMD_FAILED,
                                    "Internal error");
        }
    }
}

#ifdef IOTCONFIG_ENABLE_OTA
static void on_ota(IotclC2dEventData data) {
	const char *message = NULL;
	const char *url = iotcl_c2d_get_ota_url(data, 0);
	const char *ack_id = iotcl_c2d_get_ack_id(data);
    bool success = false;
    int needs_ota_commit = false;

    LogInfo("\n\nOTA command received\n");

    if (NULL != url) {
    	LogInfo("Download URL is: %s\r\n", url);
		const char *version = iotcl_c2d_get_ota_sw_version(data);
        if (!version) {
            success = true;
            message = "Failed to parse message";
        } else {
            // ignore wrong app versions in this application
            success = true;
            if (is_app_version_same_as_ota(version)) {
            	IOTCL_WARN(0, "OTA request for same version %s. Sending successn", version);
            } else if (app_needs_ota_update(version)) {
            	IOTCL_WARN(0, "OTA update is required for version %s.", version);
            }  else {
            	IOTCL_WARN(0, "Device firmware version %s is newer than OTA version %s. Sending failuren", APP_VERSION,
                        version);
                // Not sure what to do here. The app version is better than OTA version.
                // Probably a development version, so return failure?
                // The user should decide here.
            }

            is_downloading = true;

            if (start_ota(url) == 0) {
                needs_ota_commit = true;
                success = true;
            }

            is_downloading = false; // we should reset soon
        }

        free((void*) url);
        free((void*) version);
    } else {
        IOTCL_ERROR(0, "OTA has no URL");
        success = false;
    }

    iotcl_mqtt_send_ota_ack(ack_id,
            (success ?
                        0 :
                        IOTCL_C2D_EVT_OTA_DOWNLOAD_FAILED), message);

    if (needs_ota_commit) {
        // 5 second Delay to allow OTA ack to be sent
    	IOTCL_INFO("wait 5 seconds to commit OTA");
        vTaskDelay( pdMS_TO_TICKS( 5000 ) );
        IOTCL_INFO("committing OTA...");
        iotc_ota_fw_apply();
    }
}


/* @brief	Parse the OTA download URL into host and resource strings
 *
 * Note: The host and resource strings will be malloced, ensure to
 * free the two pointers on success
 */
static int split_url(const char *url, char **host_name, char**resource) {
    size_t host_name_start = 0;
    size_t url_len = strlen(url);

    if (!host_name || !resource) {
    	IOTCL_ERROR(0, "split_url: Invalid usage");
        return -1;
    }
    *host_name = NULL;
    *resource = NULL;
    int slash_count = 0;
    for (size_t i = 0; i < url_len; i++) {
        if (url[i] == '/') {
            slash_count++;
            if (slash_count == 2) {
                host_name_start = i + 1;
            } else if (slash_count == 3) {
                const size_t slash_start = i;
                const size_t host_name_len = i - host_name_start;
                const size_t resource_len = url_len - i;
                *host_name = malloc(host_name_len + 1); //+1 for null
                if (NULL == *host_name) {
                    return -2;
                }
                memcpy(*host_name, &url[host_name_start], host_name_len);
                (*host_name)[host_name_len] = 0; // terminate the string

                *resource = malloc(resource_len + 1); //+1 for null
                if (NULL == *resource) {
                    free(*host_name);
                    return -3;
                }
                memcpy(*resource, &url[slash_start], resource_len);
                (*resource)[resource_len] = 0; // terminate the string

                return 0;
            }
        }
    }
    return -4; // URL could not be parsed
}

static int start_ota(char *url)
{
    char *host_name;
    char *resource;
    int status;

    IOTCL_INFO("start_ota: %s", url);

    status = split_url(url, &host_name, &resource);
    if (status) {
        IOTCL_ERROR(status, "start_ota: Error while splitting the URL, code: 0x%x", status);
        return status;
    }

    status = iotc_ota_fw_download(host_name, resource);

    free(host_name);
    free(resource);

    return status;
}

static bool is_app_version_same_as_ota(const char *version) {
    return strcmp(APP_VERSION, version) == 0;
}

static bool app_needs_ota_update(const char *version) {
    return strcmp(APP_VERSION, version) < 0;
}

#endif


