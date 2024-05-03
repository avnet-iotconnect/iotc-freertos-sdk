//
// Copyright: Avnet 2023
// Created by Marven Gilhespie
//

#include "iotconnect_app.h"

#include "logging_levels.h"
/* define LOG_LEVEL here if you want to modify the logging level from the default */
#define LOG_LEVEL    LOG_INFO
#include "logging.h"

// @brief	IOTConnect configuration defined by application
#if !defined(IOTCONFIG_USE_DISCOVERY_SYNC)
static IotConnectAwsrtosConfig awsrtos_config;
#endif

static bool is_downloading = false;

MessageBufferHandle_t iotcAppQueueTelemetry = NULL;

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
    char *cpid = KVStore_getStringHeap(CS_IOTC_CPID, NULL);
    char *iotc_env = KVStore_getStringHeap(CS_IOTC_ENV, NULL);

    if (device_id == NULL || cpid == NULL || iotc_env == NULL) {
    	LogError("IOTC configuration, thing_name, cpid or env are not set\n");
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

    awsrtos_config.host = mqtt_endpoint_url;
	iotconnect_sdk_init(&awsrtos_config);
#endif


    while (1) {
        size_t n;
#define IOTC_TELEMETRY_MSG_SIZ (128)
        void *telemetryData[IOTC_TELEMETRY_MSG_SIZ];

        n = xMessageBufferReceive(iotcAppQueueTelemetry, &telemetryData, IOTC_TELEMETRY_MSG_SIZ, 0xffffffffUL);
        if (n == 0) {
            LogError("[%s] Q recv error \r\n", __func__);
            break;
        }

		IotclMessageHandle message = iotcl_telemetry_create();
		char* json_message =
				iotcApp_create_telemetry_json(
						message,
						&telemetryData, n);

		if (json_message == NULL) {
			LogError("Could not create telemetry data\n");
			vTaskDelete( NULL );
		}

		LogInfo("Telemetry: %s\n", json_message);

		iotconnect_sdk_send_packet(json_message);  // underlying code will report an error
		iotcl_destroy_serialized(json_message);

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
__weak char *iotcApp_create_telemetry_json(IotclMessageHandle msg, const void * pToTelemetryStruct, size_t siz) {

	const struct EXAMPLE_IOTC_TELEMETRY * p = NULL;

	if(siz != sizeof(const struct EXAMPLE_IOTC_TELEMETRY))
		return '\0';

	// Optional. The first time you create a data poiF\Z\nt, the current timestamp will be automatically added
    // TelemetryAddWith* calls are only required if sending multiple data points in one packet.
    iotcl_telemetry_add_with_iso_time(msg, NULL);

    iotcl_telemetry_set_number(msg, "double_value", p->doubleValue);
    iotcl_telemetry_set_bool(msg, "bool_value", p->boolValue);
    iotcl_telemetry_set_string(msg, "string_value", p->strValue);

    iotcl_telemetry_set_string(msg, "version", APP_VERSION);

    const char* str = iotcl_create_serialized_string(msg, false);

	if (str == NULL) {
		LogInfo( "serialized_string is NULL");
	}

	iotcl_telemetry_destroy(msg);
    return (char* )str;
}

__weak void iotc_process_cmd_str(IotclEventData data, char* command){
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
        command_status(data, true, command, "OK");
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
        command_status(data, true, command, "OK");
    } else if(NULL != strcasestr(command, IOTC_CMD_LED_FREQ)) {
        // Get the int following the command
        int led_freq = atoi(command + strlen(IOTC_CMD_LED_FREQ) + 1);
        if (0 != led_freq){
            set_led_freq(led_freq);
        }
#endif // IOTC_USE_LED
    } else {
        LogInfo("Command not recognized: %s", command);
        command_status(data, false, command, "Not implemented");
    }
}

/* @brief	Callback when a a cloud-to-device command is received on the subscribed MQTT topic
 */
static void on_command(IotclEventData data) {
	if (data == NULL) {
		LogWarn("on_command called with data = NULL");
		return;
	}

	char *command = iotcl_clone_command(data);

    if (NULL != command) {
        iotc_process_cmd_str(data, command);
    } else {
		LogInfo("No command, internal error");
        command_status(data, false, "?", "Internal error");
    }

    free((void*) command);
}


/* @brief	Generate a command acknowledgement message and publish it on the events topic
 *
 */
void command_status(IotclEventData data, bool status, const char *command_name, const char *message) {
    const char *ack = iotcl_create_ack_string_and_destroy_event(data, status, message);				// defined in iotc-c-lib iotconnect_evvent.c

    if (ack == NULL) {
    	LogInfo("command: no ack required");
    	return;
    }

	LogInfo("command: %s status=%s: %s\r\n", command_name, status ? "OK" : "Failed", message);
	LogInfo("Sent CMD ack: %s\r\n", ack);
	iotconnect_sdk_send_packet(ack);
	free((void*) ack);
}


#ifdef IOTCONFIG_ENABLE_OTA
static void on_ota(IotclEventData data) {
    const char *message = NULL;
    char *url = iotcl_clone_download_url(data, 0);
    bool success = false;

    LogInfo("\n\nOTA command received\n");

    if (NULL != url) {
    	LogInfo("Download URL is: %s\r\n", url);
        const char *version = iotcl_clone_sw_version(data);
        if (!version) {
            success = true;
            message = "Failed to parse message";
        } else {
        	// ignore wrong app versions in this application
            success = true;
            if (is_app_version_same_as_ota(version)) {
            	LogWarn("OTA request for same version %s. Sending successn", version);
            } else if (app_needs_ota_update(version)) {
            	LogWarn("OTA update is required for version %s.", version);
            }  else {
            	LogWarn("Device firmware version %s is newer than OTA version %s. Sending failuren", APP_VERSION,
                        version);
                // Not sure what to do here. The app version is better than OTA version.
                // Probably a development version, so return failure?
                // The user should decide here.
            }

            is_downloading = true;
            start_ota(url);
            is_downloading = false; // we should reset soon
        }


        free((void*) url);
        free((void*) version);
    } else {
        // compatibility with older events
        // This app does not support FOTA with older back ends, but the user can add the functionality
        const char *command = iotcl_clone_command(data);
        if (NULL != command) {
            // URL will be inside the command
        	LogInfo("Command is: %s", command);
            message = "Old back end URLS are not supported by the app";
            free((void*) command);
        }
    }
    const char *ack = iotcl_create_ack_string_and_destroy_event(data, success, message);
    if (NULL != ack) {
    	LogInfo("Sent OTA ack: %s", ack);
        iotconnect_sdk_send_packet(ack);
        free((void*) ack);
    }

    LogInfo("on_ota done");
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
    	LogError("split_url: Invalid usage");
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

    LogInfo ("start_ota: %s", url);

    int status = split_url(url, &host_name, &resource);
    if (status) {
        LogError("start_ota: Error while splitting the URL, code: 0x%x", status);
        return status;
    }

    iotc_ota_fw_download(host_name, resource);

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


