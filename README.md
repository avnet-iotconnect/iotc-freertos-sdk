## About

This repository contains the IoTConnect C SDK for FreeRTOS running on the following devices:
* B-U585I-IOT02A Discovery kit for IoT node with STM32U5 series
* STM32H573I-DK Discovery kit with STM32H573IIK3Q MCU 

For STM32U5 IoT Discovery Kit (B-U585I-IOT02A) support refer to the dedicated repository at [iotc-freertos-stm32-u5](https://github.com/avnet-iotconnect/iotc-freertos-stm32-u5)

For STM32H5 Discovery Kit (STM32H573I-DK) support refer to the dedicated repository at [iotc-freertos-stm32-h5](https://github.com/avnet-iotconnect/iotc-freertos-stm32-h5)


If contributing to this project, please follow the [contributing guidelines](CONTRIBUTING.md)

## Build Instructions

* If using the STM32U5, you should follow the initial [Getting_Started_Guide.md]([https://github.com/avnet-iotconnect/iotc-freertos-stm32-u5/blob/main/Getting_Started_Guide.md]). Note that software X509 mutual authentication is also supported with the board but the steps are described in this document.
* Obtain the software corresponding to your board by using one of the following methods:
  * Option 1) Download and extract the project package for the target board from the [Releases](https://github.com/avnet-iotconnect/iotc-freertos-sdk/releases) page.
  * Option 2) Clone this repository. After cloning, execute [setup-project.sh](scripts/setup-project.sh]) to setup the environment.
  * Once the setup-project.sh script has executed the next stage is to gather any remaining protocol source files required for the demo task in iotconnect_app.c. This is acheived by executing rtosPull.sh. To date, when queried for which extra sources are required, only answer "yes" to FreeRTOSPlus & coreHTTP.
  * To integrate the task within a wider project, 5 coding tasks are required:
  * 1. Add the iotconnect_app task to the initialisation of other tasks.
    2. Connection code
    3. Telemetry code
    4. Commands code
    5. OTA code
* On Windows, a bash interpreter is required (such as MSYS2). Alternatively, a Linux distribution can be installed through the Windows Store and can be run via Windows Subsystem for Linux.

## Creating Self-Signed x509 Device Certificates
* Download and follow instructions at 
iotc-c-lib's [ecc-certs section](https://github.com/avnet-iotconnect/iotc-c-lib/tree/master/tools/ecc-certs)
to upload and validate your CA certificate in IoTConnect. Set up your device and template in IoTConnect.
* If your project Azure RTOS baseline does not support ECC certificates, replace this line in `function device`:

```shell script
openssl ecparam -name secp256k1 -genkey -noout -out "${CPID}/${name}-key.pem"
```

whith this:

```shell script
# for mimxrt1060, the 2048 key size makes processing TLS handshake too slow to complete within 60 seconds
# and that will cause the server to disconnect us
openssl genrsa -out "${CPID}/${name}-key.pem" 1024
# for all others, 2048 can be used to increase security, but 1024 will make the connection process faster:
openssl genrsa -out "${CPID}/${name}-key.pem" 2048
```
* Generate the device certificates per iotc-c-lib's [ecc-certs section](https://github.com/avnet-iotconnect/iotc-c-lib/tree/master/tools/ecc-certs) instructions
* Generate .der format certificate and C snippets using the following example using your CPID and device ID in place: 

```shell script
cd <your_cpid>
$device=<duid> # IoTConnect device unique id
openssl rsa -inform PEM -outform DER -in $device-key.pem -out $device-key.der
openssl x509 -outform DER -inform PEM -in $device-crt.pem -out $device-crt.der
xxd -i $device-crt.der > $device-crt.c
xxd -i $device-key.der > $device-key.c
```
* The generated *.c files will have code snippets that you can use to assign to IOTCONNECT_CONFIG
following the example in iotconnect_app.c

## License
Use of MICROSOFT AZURE RTOS software is restricted under the corresponding license for that software at the [azrtos-licenses](azrtos-licenses/) directory.

This repo's derivative work from Azure RTOS distribution also falls under the same MICROSOFT AZURE RTOS license.

# Notes and Known issues
