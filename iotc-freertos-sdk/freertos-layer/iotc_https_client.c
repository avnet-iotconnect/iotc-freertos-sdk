/*
 * iotc_https_client.c
 *
 *  Created on: Nov 3, 2023
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

#include "iotconnect_config.h"
#include "iotc_https_client.h"

#ifndef pdTICKS_TO_MS
    #define pdTICKS_TO_MS( xTicks )       ( ( TickType_t ) ( ( uint64_t ) ( xTicks ) * 1000 / configTICK_RATE_HZ ) )
#endif

#define HTTPS_BUFFER_SZ			3072


// Variables
static uint32_t ulGlobalEntryTimeTicks;			// Timer epoch in ticks since start of a HTTP_Send
static TransportInterface_t transportInterface = { 0 };
static HTTPRequestHeaders_t requestHeaders = { 0 };
static HTTPResponse_t response = { 0 };
static PkiObject_t pxRootCaChain[1];


// Prototypes
static NetworkContext_t *configure_transport(void);
static uint32_t get_time_in_ms_since_http_request_start(void);

/*
 *
 */
void iotconnect_https_init(PkiObject_t root_ca)
{
	pxRootCaChain[0] = root_ca;
}


/*
 *
 */
int32_t iotc_send_http_request(IotConnectHttpResponse *iotc_response,
						  const char *server_host, int port,
		                  const char *method, const char *path,
						  char *user_buffer, size_t user_buffer_len)
{
	int32_t returnStatus = EXIT_SUCCESS;
    /* Network connection context. This is an opaque object that actually points to an mbedTLS structure that
     * manages the TLS connection */
    NetworkContext_t *pNetworkContext;
    /* Status of a network connect request */
    TlsTransportStatus_t xNetworkStatus;
    /* Configurations of the initial request headers that are passed to #HTTPClient_InitializeRequestHeaders. */
    HTTPRequestInfo_t requestInfo;
    /* Represents a response returned from an HTTP server. */
    HTTPResponse_t response;
    /* Represents header data that will be sent in an HTTP request. */
    HTTPRequestHeaders_t requestHeaders;
    /* Return value of all methods from the HTTP Client library API. */
    HTTPStatus_t httpStatus = HTTPSuccess;

    assert( method != NULL );
    assert( path != NULL );

    pNetworkContext = configure_transport();

    if (pNetworkContext == NULL) {
    	IOTCL_ERROR(-1, "failed to configure network context");
    	return EXIT_FAILURE;
    }

    xNetworkStatus = mbedtls_transport_connect( pNetworkContext,
                                            server_host,
                                            port,
                                            0, 0 );

    if (xNetworkStatus != TLS_TRANSPORT_SUCCESS) {
    	IOTCL_ERROR(-1, "Failed to connect to HTTPS server :5s", server_host);
    	mbedtls_transport_free(pNetworkContext);
        return EXIT_FAILURE;
    }

    /* Initialize all HTTP Client library API structs to 0. */
    ( void ) memset( &transportInterface, 0, sizeof( transportInterface ) );
    ( void ) memset( &requestInfo, 0, sizeof( requestInfo ) );
    ( void ) memset( &requestHeaders, 0, sizeof( requestHeaders ) );
    ( void ) memset( &response, 0, sizeof( response ) );

    transportInterface.recv = mbedtls_transport_recv;
    transportInterface.send = mbedtls_transport_send;
    transportInterface.writev = NULL;
    transportInterface.pNetworkContext = pNetworkContext;

    /* Initialize the request object. */
    requestInfo.pHost = server_host;
    requestInfo.hostLen = strlen(server_host);

    requestInfo.pMethod = method;
    requestInfo.methodLen = strlen(method);
    requestInfo.pPath = path;
    requestInfo.pathLen = strlen(path);

    /* Set "Connection" HTTP header to "keep-alive" so that multiple requests
     * can be sent over the same established TCP connection. */
    requestInfo.reqFlags = HTTP_REQUEST_KEEP_ALIVE_FLAG;

    char *https_buffer = pvPortMalloc(HTTPS_BUFFER_SZ);

    /* Set the buffer used for storing request headers. */
    requestHeaders.pBuffer = https_buffer;
    requestHeaders.bufferLen = HTTPS_BUFFER_SZ -1;

    // We save the current time here so that timeouts in HTTP_Send are relative to it.
	ulGlobalEntryTimeTicks = ( uint32_t )  xTaskGetTickCount();

    /* Set time function for retry timeout on receiving the response. */
    response.getTime = get_time_in_ms_since_http_request_start;
	response.pBuffer = (uint8_t *)https_buffer;
    response.bufferLen = HTTPS_BUFFER_SZ -1;

    httpStatus = HTTPClient_InitializeRequestHeaders( &requestHeaders, &requestInfo );

    if( httpStatus == HTTPSuccess ) {
        IOTCL_INFO("Sending HTTPS %s request to %s %s...", requestInfo.pMethod, server_host, requestInfo.pPath);
        IOTCL_INFO("requestHeaders: %s", requestHeaders.pBuffer);

        /* Send the request and receive the response. */
        httpStatus = HTTPClient_Send( &transportInterface,
                                      &requestHeaders,
                                      ( uint8_t * ) "\r\n",
                                      2,
                                      &response,
                                      0 );
    } else {
        IOTCL_ERROR(httpStatus, "Failed to initialize HTTP request headers: Error=%s", HTTPClient_strerror(httpStatus));        
    }

    if( httpStatus == HTTPSuccess ) {
    	iotc_response->data = response.pBody;

        IOTCL_INFO("Received HTTP response from %s %s...", server_host, requestInfo.pPath);
        IOTCL_INFO("Response Headers:\r\n%s", response.pHeaders);
        IOTCL_INFO("Response Status:\r\n%u", response.statusCode);
        IOTCL_INFO("Response Body:\r\n%s", response.pBody);
    	IOTCL_INFO("\r\n-------------------------");
    } else {
    	iotc_response->data = NULL;

        IOTCL_ERROR(httpStatus, "Failed to send HTTP %s request to %s %s: Error=%s.",
                    requestInfo.pMethod,
                    server_host,
                    requestInfo.pPath,
                    HTTPClient_strerror(httpStatus));
    }

    mbedtls_transport_disconnect(pNetworkContext);
    mbedtls_transport_free(pNetworkContext);

    if( httpStatus != HTTPSuccess ) {
        returnStatus = EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


/*
 *
 */
static NetworkContext_t *configure_transport(void)
{
    MQTTStatus_t xMQTTStatus = MQTTSuccess;
    TlsTransportStatus_t xTlsStatus = TLS_TRANSPORT_CONNECT_FAILURE;
    BaseType_t xExitFlag = pdFALSE;
    NetworkContext_t *pxNetworkContext;

    if (pxRootCaChain[0].xForm == OBJ_FORM_NONE || pxRootCaChain[0].uxLen == 0) {
    	IOTCL_ERROR(0, "HTTPS CA Certificate not set");
    	return NULL;
    }

	pxNetworkContext = mbedtls_transport_allocate();

	if( pxNetworkContext == NULL ) {
		IOTCL_ERROR(0, "Failed to allocate an mbedtls transport context." );
		xMQTTStatus = MQTTNoMemory;
		return NULL;
	}

    if( xMQTTStatus == MQTTSuccess ) {
        xTlsStatus = mbedtls_transport_configure( pxNetworkContext,
                                                  NULL,					//pcAlpnProtocols,
                                                  NULL,
                                                  NULL,
												  pxRootCaChain,
                                                  1 );

        if( xTlsStatus != TLS_TRANSPORT_SUCCESS ) {
            IOTCL_ERROR(xTlsStatus, "Failed to configure mbedtls transport." );
            xMQTTStatus = MQTTBadParameter;
            mbedtls_transport_free(pxNetworkContext);
            return NULL;
        }
    }

   return pxNetworkContext;
}


/*
 *
 */
static uint32_t get_time_in_ms_since_http_request_start( void )
{
    uint32_t ulTimeMs = 0UL;

    /* Determine the elapsed time in the application */
    ulTimeMs = ( uint32_t ) pdTICKS_TO_MS( (uint32_t)xTaskGetTickCount() - ulGlobalEntryTimeTicks);

    return ulTimeMs;
}

