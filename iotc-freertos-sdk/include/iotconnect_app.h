#ifndef IOTCONNECT_APP_H
#define IOTCONNECT_APP_H

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

// Constants
#define APP_VERSION 			"05.09.24"		// Version string
#define MQTT_PUBLISH_PERIOD_MS 	( 3000 )		// Size of statically allocated buffers for holding topic names and payloads.

#ifndef IOTC_APP_QUEUE_SIZE_TELEMETRY
#define IOTC_APP_QUEUE_SIZE_TELEMETRY 5
#endif

#ifndef pkcs11_MQTT_ROOT_CA_CERT_LABEL
#define pkcs11_MQTT_ROOT_CA_CERT_LABEL                          "root_ca_cert"
#endif

#ifndef TLS_MQTT_ROOT_CA_CERT_LABEL
#define TLS_MQTT_ROOT_CA_CERT_LABEL     pkcs11_MQTT_ROOT_CA_CERT_LABEL
#endif

// CMD CONFIG
#define IOTC_USE_LED

// IOT COMMANDS
#define IOTC_CMD_PING "ping"

// LED COMMANDS
#ifdef IOTC_USE_LED
#define IOTC_CMD_LED_RED   "led-red"
#define IOTC_CMD_LED_GREEN "led-green"
#define IOTC_CMD_LED_FREQ  "led-freq"

__weak void set_led_red(bool state);
__weak void set_led_green(bool state);
__weak void set_led_freq(int freq);
#endif // IOTC_USE_LED

// Prototypes
extern MessageBufferHandle_t iotcAppQueueTelemetry;
__weak void iotcApp_create_and_send_telemetry_json(
		const void *pToTelemetryStruct, size_t siz);
static void on_command(IotclC2dEventData data);
void command_status(IotclC2dEventData data, bool status,
		const char *command_name, const char *message);
static void on_ota(IotclC2dEventData data);
static int split_url(const char *url, char **host_name, char**resource);
static bool is_app_version_same_as_ota(const char *version);
static bool app_needs_ota_update(const char *version);
static int start_ota(char *url);

// Include libs checks
#ifndef MBEDTLS_CIPHER_MODE_CBC
#error missing required MBEDTLS define
#endif
#ifndef MBEDTLS_CIPHER_MODE_CFB
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CIPHER_MODE_CTR
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CIPHER_MODE_OFB
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CIPHER_MODE_XTS
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CIPHER_NULL_CIPHER
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CIPHER_PADDING_PKCS7
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CIPHER_PADDING_ONE_AND_ZEROS
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CIPHER_PADDING_ZEROS_AND_LEN
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CIPHER_PADDING_ZEROS
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_SECP192R1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_SECP224R1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_SECP384R1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_SECP521R1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_SECP192K1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_SECP224K1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_SECP256K1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_BP256R1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_BP384R1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_BP512R1_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_CURVE25519_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECP_DP_CURVE448_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_ECDSA_DETERMINISTIC
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_PK_PARSE_EC_EXTENDED
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_GENPRIME
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_DTLS_CONNECTION_ID
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_PROTO_TLS1_3_EXPERIMENTAL
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_PROTO_DTLS
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_DTLS_SRTP
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_EXPORT_KEYS
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_X509_RSASSA_PSS_SUPPORT
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CCM_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CHACHA20_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_CHACHAPOLY_C
#error "missing required MBEDTLS define"
#endif

#if 0  // FIXME: Not used by U5
#ifndef MBEDTLS_DES_C
#error "missing required MBEDTLS define"
#endif
#endif

#ifndef MBEDTLS_DHM_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_HKDF_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_HMAC_DRBG_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_NIST_KW_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_MD5_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_PKCS5_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_PKCS12_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_POLY1305_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_COOKIE_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_SSL_TICKET_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_X509_CRL_PARSE_C
#error "missing required MBEDTLS define"
#endif
#ifndef MBEDTLS_X509_CSR_PARSE_C
#error "missing required MBEDTLS define"
#endif



#endif // IOTCONNECT_APP_H
