/*
 * Copyright: Avnet 2024
 * Created by mgilhespie on 2/21/24
 */

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "mbedtls_transport.h"
#include "core_http_client.h"
#include "ota_pal.h"
#include "iotconnect_certs.h"
#include "kvstore.h"

#include "iotcl_log.h"


#define DEVICE_ID_MAX_LEN 129
#define TOPIC_STR_MAX_LEN (DEVICE_ID_MAX_LEN + 20)

#define S3_RANGE_RESPONSE_PREFIX "bytes 0-0/"
// 9 megabytes will be 7 digits
#define DATA_BYTE_SIZE_CHAR_MAX (sizeof(S3_RANGE_RESPONSE_PREFIX) + 7)

// NOTE: If this chunk size is 4k or more, this error happens during initial chunk download:
// Failed to read data: Error: SSL - Bad input parameters to function : <No-Low-Level-Code>. (mbedtls_transport.c:1649)
#define DATA_CHUNK_SIZE (1024 * 4)

#define HEADER_BUFFER_LENGTH 2048
static uint8_t buff_headers[HEADER_BUFFER_LENGTH];

#define RESPONSE_BUFFER_LENGTH (DATA_CHUNK_SIZE + 2048) /* base response buffer on chunk size and add a little extra */
static uint8_t buff_response[RESPONSE_BUFFER_LENGTH];


static void setup_request(HTTPRequestInfo_t* request, const char* method, const char* host, const char* path) {
    request->pMethod = method;
    request->methodLen = strlen(method);
    request->pPath = path;
    request->pathLen = strlen(path);
    request->pHost = host;
    request->hostLen = strlen(host);
    request->reqFlags = HTTP_REQUEST_KEEP_ALIVE_FLAG;
}

int iotc_ota_fw_download(const char* host, const char* path) {
	TlsTransportStatus_t tls_transport_status;
	HTTPStatus_t http_status;
	OtaPalStatus_t pal_status;
	const char * alpn_protocols[] = {  NULL };

    NetworkContext_t* network_conext = mbedtls_transport_allocate();
    if (NULL == network_conext) {
        IOTCL_ERROR(0, "Failed to allocate network context!");
        return -1;
    }

	PkiObject_t ca_certificates[] = {PKI_OBJ_PEM((const unsigned char *)STARFIELD_ROOT_CA_G2, sizeof(STARFIELD_ROOT_CA_G2))};

    /* ALPN protocols must be a NULL-terminated list of strings. */
    tls_transport_status = mbedtls_transport_configure(
        network_conext,
		alpn_protocols,
        NULL,
        NULL,
        ca_certificates,
        1
    );
    if( TLS_TRANSPORT_SUCCESS != tls_transport_status) {
        IOTCL_ERROR(tls_transport_status, "Failed to configure mbedtls transport");
        return -1;
    }

    tls_transport_status = mbedtls_transport_connect(
    	network_conext,
    	host,
        443,
        10000,
		10000
    );
    if (TLS_TRANSPORT_SUCCESS != tls_transport_status) {
        IOTCL_ERROR(tls_transport_status, "HTTPS: Failed to connect");
        return -1;
    }

    TransportInterface_t transport_if = {0};
	transport_if.pNetworkContext = network_conext;
	transport_if.send = mbedtls_transport_send;
	transport_if.recv = mbedtls_transport_recv;

    static HTTPResponse_t response = {0};
    response.pBuffer = buff_response;
    response.bufferLen = sizeof(buff_response);

    HTTPRequestHeaders_t headers = {0};
    headers.pBuffer = buff_headers;
	headers.bufferLen = sizeof(buff_headers);


    // When using S3, use a GET with range 0-0 and then the returned size will be like bytes 0-0/XXXX where X is the actual size
	// When using Azure Blob, use a HEAD with the URL in question and the Content-Length will contain the size.
    HTTPRequestInfo_t request = { 0 };
    setup_request(&request, HTTP_METHOD_GET, host, path);

    http_status = HTTPClient_InitializeRequestHeaders( &headers, &request);
	if (0 != http_status) {
    	IOTCL_ERROR(http_status, "HTTP failed to initialize headers! Error: %s", HTTPClient_strerror(http_status));
    	return -1;
	}

	http_status = HTTPClient_AddRangeHeader(&headers, 0, 0);
	if (0 != http_status) {
		IOTCL_ERROR(http_status, "HTTP failed to add initial range header for size query! Error: %s", HTTPClient_strerror(http_status));
		return -1;
	}

	http_status = HTTPClient_Send(
		&transport_if,
		&headers, /* HTTPRequestHeaders_t  pRequestHeaders*/
		NULL, /*const uint8_t * pRequestBodyBuf*/
		0, /* size_t reqBodyBufLen*/
		&response,
		0 /* uint32_t sendFlags*/
	);
	if (0 != http_status) {
    	IOTCL_ERROR(http_status, "HTTP Send Error: %s", HTTPClient_strerror(http_status));
	}

	// NOTE: AWS S3 may be returning Content-Range
	const char* data_length_str = NULL;
	size_t data_length_str_len = 0;

	// When using S3, use a GET with range 0-0 and then the returned size will be like bytes 0-0/XXXX where X is the actual size
	http_status = HTTPClient_ReadHeader( &response,
		"Content-Range",
		sizeof("Content-Range") - 1,
		&data_length_str,
		&data_length_str_len
	);
	if (0 != http_status) {
    	IOTCL_ERROR(http_status, "HTTP Error while obtaining headers: %s", HTTPClient_strerror(http_status));
	}

	if (response.statusCode != 200) {
		IOTCL_INFO("Response status code is: %u", response.statusCode);
	}

	if (NULL == data_length_str || 0 == data_length_str_len) {
		IOTCL_ERROR(data_length_str_len, "Could not obtain data length!");
		return -1;
	}

	IOTCL_INFO("Response range reported: %.*s", data_length_str_len, data_length_str);

	if (data_length_str_len > DATA_BYTE_SIZE_CHAR_MAX) {
		IOTCL_INFO("Unsupported data length: %lu", data_length_str_len);
		return -1;
	}

	int data_length = 0;
	char data_length_buffer[DATA_BYTE_SIZE_CHAR_MAX + 1]; // for scanf to deal with a null terminated string
	strncpy(data_length_buffer, data_length_str, data_length_str_len);
	if (1 != sscanf(data_length_buffer, S3_RANGE_RESPONSE_PREFIX"%d", &data_length)) {
		IOTCL_INFO("Could not convert data length to number");
		return -1;
	}

	IOTCL_INFO("Response data length (number) is %d", data_length);

	OtaFileContext_t file_context;
	file_context.fileSize = (uint32_t)data_length;
	file_context.pFilePath = (uint8_t *)"b_u585i_iot02a_ntz.bin";
	file_context.filePathMaxSize = (uint16_t)strlen((const char*)file_context.pFilePath);

	pal_status = otaPal_CreateFileForRx(&file_context);
	if (OtaPalSuccess != pal_status) {
		IOTCL_ERROR(pal_status, "OTA failed to create file. Error: 0x%x", pal_status);
		return -1;
	}
	// OtaPalImageState_t image_state = otaPal_GetPlatformImageState( OtaFileContext_t * const pFileContext );

	int progress_ctr = 0;
	for (int data_start = 0; data_start < data_length; data_start += DATA_CHUNK_SIZE) {
		int data_end = data_start + DATA_CHUNK_SIZE;
		if (data_end > data_length) {
			data_end = data_length;
		}

		memset(&request, 0, sizeof(request));
		memset(&headers, 0, sizeof(headers));
	    headers.pBuffer = buff_headers;
		headers.bufferLen = sizeof(buff_headers);

	    setup_request(&request, HTTP_METHOD_GET, host, path);

	    http_status = HTTPClient_InitializeRequestHeaders(&headers, &request);
		if (HTTPSuccess != http_status) {
	    	IOTCL_ERROR(http_status, "HTTP failed to initialize headers! Error: %s", HTTPClient_strerror(http_status));
	    	return -1;
		}
		http_status = HTTPClient_AddRangeHeader(&headers, data_start, data_end - 1);
		if (HTTPSuccess != http_status) {
			IOTCL_ERROR(http_status, "HTTP failed to add range header! Error: %s", HTTPClient_strerror(http_status));
			return -1;
		}

		int tries_remaining = 30;
        do {
            http_status = HTTPClient_Send(
                &transport_if,
                &headers, /* HTTPRequestHeaders_t  pRequestHeaders*/
                NULL, /*const uint8_t * pRequestBodyBuf*/
                0, /* size_t reqBodyBufLen*/
                &response,
                0 /* uint32_t sendFlags*/
            );

            // we need to get at least one successful fetch, and if we do we can try back off.
            // this part will trigger on 100th try.
            if (0 != data_start && HTTPNetworkError == http_status) {
                if (0 == tries_remaining) {
                    IOTCL_ERROR(http_status, "HTTP range %d-%d send error: %s", data_start, data_end - 1, HTTPClient_strerror(http_status));
                    return -1;
                }

                IOTCL_ERROR(http_status, "Failed to get chunk range %d-%d. Reconnecting...", data_start, data_end - 1);
                mbedtls_transport_disconnect(network_conext);
                tls_transport_status = mbedtls_transport_connect(
                	network_conext,
                	host,
                    443,
                    10000,
            		10000
                );
                tries_remaining--;
            } else if (HTTPSuccess != http_status) {
                IOTCL_ERROR(http_status, "HTTP range %d-%d send error: %s", data_start, data_end - 1, HTTPClient_strerror(http_status));
                return -1;
            }
        } while (http_status == HTTPNetworkError);

		if (progress_ctr % 30 == 29) {
		    IOTCL_INFO("Progress %d%%...", data_start * 100 / data_length);
			progress_ctr = 0;
		} else {
			progress_ctr++;
		}


	    int16_t bytes_written = otaPal_WriteBlock(
	    	&file_context,
			(uint32_t) data_start,
			(uint8_t*) response.pBody,
			(uint32_t) response.bodyLen
	    );
	    if (bytes_written != (int16_t) response.bodyLen) {
	    	IOTCL_INFO("Expected to write %d bytes, but wrote %u!", response.bodyLen, bytes_written);
	    	return -1;
	    }
	}
    mbedtls_transport_disconnect(network_conext);

    IOTCL_INFO("OTA download complete. Launching the new image!");

    pal_status = otaPal_CloseFile(&file_context);
	if (OtaPalSuccess != pal_status) {
		IOTCL_ERROR(pal_status, "OTA failed close the downloaded firmware file");
	}

    return 0;
}

/*
 *
 */
int iotc_ota_fw_apply(void) {
	OtaPalStatus_t pal_status;

    IOTCL_INFO("OTA: Applying firmware. Resetting the board.\r\n");

	//pal_status = otaPal_ActivateNewImage((OtaFileContext_t * const)NULL);
	if (OtaPalSuccess != pal_status) {
		IOTCL_ERROR(pal_status, "OTA failed activate the downloaded firmware");
	}

}

#include "sys_evt.h"
#include "core_mqtt_agent.h"
#include "subscription_manager.h"
#include "mqtt_agent_task.h"

/*
AWS S3 does not have an official limit for the length of a presigned URL. However, some have encountered a presigned URL for an S3 object that was 1669 characters long, which is close to the unofficial URL length limit of 2 KB.
Presigned URLs (PUT & GET) do not support limiting the file size. A PUT HTTP request using the presigned URL is a "single"-part upload, and the object size is limited to 5 GB.
Signed URLs are generated with specific access permissions, expiration times, and cryptographic signatures. This ensures that only authorized users can access the content.
*/
#define JSON_OBJ_URL "\"url\":\""
#define JSON_OBJ_FILENAME "\"fileName\":\""
#define MAX_URL_LEN 2000
char url_buff[MAX_URL_LEN + 1];
static bool copy_until_char(char * target, const char* source, char terminator) {
    size_t src_len = strlen(source);
    target[0] = 0;
    for(size_t i = 0; i < src_len; i++) {
        int src_ch = source[i];
        if (terminator == src_ch) {
            target[i] = 0;
            return true;
        } else {
            target[i] = source[i];
        }
    }
    return false; // ran past the end of source
}
static void on_c2d_message( void * subscription_context, MQTTPublishInfo_t * publish_info ) {
    (void) subscription_context;

    if (!publish_info) {
        IOTCL_ERROR(publish_info, "on_c2d_message: publish_info is NULL");
        return;
    }
    
    IOTCL_INFO("<<< %d.%s", publish_info->payloadLength, publish_info->pPayload);

    char* payload = (char *)publish_info->pPayload;
    payload[publish_info->payloadLength] = 0; // terminate the string just in case. Don't really care about the last char for now
    char *url_part = strstr(payload, JSON_OBJ_URL);
    if (!url_part) {
        IOTCL_ERROR(0, "on_c2d_message: command received, url_part is NULL");
        return;
    }

    IOTCL_INFO("on_c2d_message: OTA received");

    if (!copy_until_char(url_buff, &url_part[strlen(JSON_OBJ_URL)], '"')) {
        IOTCL_ERROR(0, "on_c2d_message: Publish info is NULL?");
    }
    
    IOTCL_INFO("OTA URL: %s", url_buff);

    char file_name_buff[100]; // TODO: limit file name length
    char *fn_part = strstr(publish_info->pPayload, JSON_OBJ_FILENAME);
    if (!fn_part) {
        IOTCL_ERROR(0, "on_c2d_message: missing filename?");
        return;
    }
    if (!copy_until_char(file_name_buff, &fn_part[strlen(JSON_OBJ_FILENAME)], '"')) {
		IOTCL_ERROR(0, "on_c2d_message: Publish info is NULL?");
        return;
    }
    
    IOTCL_INFO("OTA File: %s", file_name_buff);
}


/*
 *
 */
static bool subscribe_to_c2d_topic(void)
{
    char device_id[DEVICE_ID_MAX_LEN];
    char sub_topic[TOPIC_STR_MAX_LEN];
    if (KVStore_getString(CS_CORE_THING_NAME, device_id, DEVICE_ID_MAX_LEN) <= 0) {
	    IOTCL_ERROR(0, "Unable to get device ID");
	    return false;
	}
    sprintf(sub_topic, "iot/%s/cmd", device_id);

    MQTTAgentHandle_t agent_handle = xGetMqttAgentHandle();
    if (agent_handle == NULL )  {
	    IOTCL_ERROR(0, "Unable to get agent handle");
	    return false;
    }

    MQTTStatus_t mqtt_status = MqttAgent_SubscribeSync( agent_handle,
		sub_topic,
		1 /* qos */,
		on_c2d_message,
		NULL
    );
    if (MQTTSuccess != mqtt_status) {
        IOTCL_ERROR(mqtt_status, "Failed to SUBSCRIBE to topic");
        return false;
    }

    IOTCL_INFO("Subscribed to c2d topic %s", sub_topic);

    return true;
}

static bool is_mqtt_connected(void)
{
	/* Wait for MQTT to be connected */
	EventBits_t uxEvents = xEventGroupWaitBits(xSystemEvents,
											   EVT_MASK_MQTT_CONNECTED,
											   pdFALSE,
											   pdTRUE,
											   0);

	return ((uxEvents & EVT_MASK_MQTT_CONNECTED) == EVT_MASK_MQTT_CONNECTED);
}
