#!/bin/bash
YELLOW='\033[33m'
BLACK_ON_GREEN='\033[1;30;42m'
NC='\033[0m' # No Color

# This allows this script to exec from anywhere
current_dir=$(realpath $(pwd))
script_dir=$(realpath $(dirname $0))
cd $script_dir

# Dependency Check
deps="git"
missing_deps=false

for d in $deps
do
    if ! which $d > /dev/null; then
        echo "Cannot find $d to execute."
        missing_deps=true
    fi
done

if $missing_deps; then
    echo "You are missing dependencies! Please install them before running this script."
    exit
fi

# Choose libs to install
opt_fileout="/tmp/iotcChoices"
rm -rf $opt_fileout
# USE PROPER STUBS HERE!
opt_slugs="FreeRTOS-Plus coreMQTT-Agent coreSNTP coreHTTP corePKCS11 mbedtls lwip littlefs"

for s in $opt_slugs
do
	echo "Do you want to enable: $s?"
	echo "y/N"
	read slug_conf

	if [[ "y" == $slug_conf || "Y" == $slug_conf ]]
	then
		echo "Adding $s..."
		echo
		echo -n "$s " >> $opt_fileout
	elif [[ "n" == $slug_conf || "N" == $slug_conf || -z $slug_conf ]]
	then
		echo "Skipping $s..."
		echo
	else
		echo "INVALID OPTION! Exiting!"
		echo
		rm -rf $opt_fileout
		exit
	fi
done

if [[ -f $opt_fileout ]]
then
    CHOICES=$(cat $opt_fileout)
    echo "Pulling: $CHOICES"
    rm $opt_fileout
else
    echo "No options file detected at: $opt_fileout. Are you sure you don't need anything?"
fi

echo -e "${BLACK_ON_GREEN=}Checking out sources${NC}"

if [[ $CHOICES == *"FreeRTOS-Plus"* ]]; then
    for i in $CHOICES
    do
       case $i in
        "coreMQTT-Agent")
            FREERTOS_SUBMOD_PATHS+="FreeRTOS-Plus/Source/Application-Protocols/coreMQTT-Agent " ;;
        "coreSNTP")
            FREERTOS_SUBMOD_PATHS+="FreeRTOS-Plus/Source/Application-Protocols/coreSNTP " ;;
        "coreHTTP")
            FREERTOS_SUBMOD_PATHS+="FreeRTOS-Plus/Source/Application-Protocols/coreHTTP " ;;
        "corePKCS11")
            FREERTOS_SUBMOD_PATHS+="FreeRTOS-Plus/Source/corePKCS11 " ;;
        "mbedtls")
            FREERTOS_SUBMOD_PATHS+="FreeRTOS-Plus/ThirdParty/mbedtls " ;;
       esac
    done

    if [ ! -d iotc-freertos ]; then
        git clone --sparse --branch 202212.00 git@github.com:FreeRTOS/FreeRTOS.git iotc-freertos
        cd iotc-freertos
        git sparse-checkout init --cone
        git submodule init $FREERTOS_SUBMOD_PATHS
        git submodule update
        if [[ $CHOICES == *"coreMQTT-Agent"* ]]; then
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreMQTT-Agent/ sparse-checkout init --cone
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreMQTT-Agent/ sparse-checkout add source
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreMQTT-Agent/ submodule init source/dependency/coreMQTT/
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreMQTT-Agent/ submodule update source/dependency/coreMQTT/
        fi
        if [[ $CHOICES == *"coreSNTP"* ]]; then
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreSNTP/ sparse-checkout init --cone
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreSNTP/ sparse-checkout add source
        fi
        if [[ $CHOICES == *"coreHTTP"* ]]; then
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreHTTP/ sparse-checkout init --cone
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreHTTP/ sparse-checkout add source
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreHTTP/ submodule init source/dependency/3rdparty/llhttp
            git -C FreeRTOS-Plus/Source/Application-Protocols/coreHTTP/ submodule update source/dependency/3rdparty/llhttp
        fi
        if [[ $CHOICES == *"corePKCS11"* ]]; then
            git -C FreeRTOS-Plus/Source/corePKCS11/ sparse-checkout init --cone
            git -C FreeRTOS-Plus/Source/corePKCS11/ sparse-checkout add source/dependency source/include source/core_pkcs11.c source/core_pki_utils.c
            git -C FreeRTOS-Plus/Source/corePKCS11/ submodule init source/dependency/3rdparty/pkcs11
            git -C FreeRTOS-Plus/Source/corePKCS11/ submodule update source/dependency/3rdparty/pkcs11
        fi
        if [[ $CHOICES == *"mbedtls"* ]]; then
            git -C FreeRTOS-Plus/ThirdParty/mbedtls sparse-checkout init --cone
            git -C FreeRTOS-Plus/ThirdParty/mbedtls sparse-checkout add library include
        fi

        git checkout --

        if [[ $CHOICES == *"coreMQTT-Agent"* ]]; then
                git -C FreeRTOS-Plus/Source/Application-Protocols/coreMQTT-Agent/source/dependency/coreMQTT/ sparse-checkout init --cone
                git -C FreeRTOS-Plus/Source/Application-Protocols/coreMQTT-Agent/source/dependency/coreMQTT/ sparse-checkout add source
                git checkout --
        fi
        cd ..
    fi
fi

git submodule init iotc-c-lib-sparse
git submodule update iotc-c-lib-sparse
git -C iotc-c-lib-sparse sparse-checkout init --cone
git -C iotc-c-lib-sparse sparse-checkout add core lib modules tools

git submodule init cJSON
git submodule update cJSON
git -C cJSON sparse-checkout init --no-cone
git -C cJSON sparse-checkout set cJSON.c cJSON.h

if [[ $CHOICES == *"lwip"* ]]; then
    git submodule init lwip
    git submodule update lwip
    git -C lwip sparse-checkout init
    git -C lwip sparse-checkout set src/netif \
	    src/netif/ppp \
	    src/netif/ppp/polarssl \
	    src/core \
	    src/api \
	    src/include \
	    src/apps/smtp \
	    src/apps/mdns \
	    src/apps/snmp \
	    src/apps/tftp \
	    src/apps/netbiosns \
	    src/apps/altcp_tls \
	    src/apps/sntp \
	    src/apps/lwiperf \
	    src/apps/mqtt
fi

if [[ $CHOICES == *"littlefs"* ]]; then
    git submodule init littlefs
    git submodule update littlefs
    git -C littlefs sparse-checkout init
    git -C littlefs sparse-checkout set bd lfs.c  lfs.h  lfs_util.c  lfs_util.h
fi

if [[ $CHOICES == *"FreeRTOS-Plus"* ]]; then
    echo -e "${BLACK_ON_GREEN=}Pruning extra files & directories${NC}"
    find iotc-freertos -type f -and -not \( -name *.h -or -name *.c \) -and -not -path "*.git*" -exec rm {} \;
fi

echo -e "${BLACK_ON_GREEN=}Building smart-inc${NC}"

if [[ $CHOICES == *"FreeRTOS-Plus"* ]]; then
    INCLUDES+=( $(find iotc-freertos -type d -and -name include) )
fi

INCLUDES+=( $(find iotc-c-lib-sparse -type d -and -name include) )
INCLUDES+=( $(find iotc-c-lib-sparse -type d -and -name device-rest-api) )
INCLUDES+=( $(find iotc-c-lib-sparse -type d -and -name heap-tracker) )
INCLUDES+=( $(find iotc-freertos-sdk -type d -and -name include) )
INCLUDES+=( "iotc-freertos-sdk/freertos-layer/sntp" )

if [[ $CHOICES == *"lwip"* ]]; then
    INCLUDES+=( $(find lwip -type d -and -name include) )
fi

#INCLUDES+=( $(find config -type d -and -name include) )

INCLUDES+=( "cJSON" )

if [[ $CHOICES == *"littlefs"* ]]; then
    INCLUDES+=( "littlefs" )
fi

mkdir -p smart-inc
TOPDIR=$(pwd)
for i in "${INCLUDES[@]}"
do
    cd $i
    cp -srR $(realpath .)/* $TOPDIR/smart-inc/
    cd $TOPDIR
done
find smart-inc -not -type d -and -not -name *.h -exec rm {} \;

# Check for missing headers
cd smart-inc
SMARTHEADERS=( $(find -name "*.h") )
cd ..
if [[ $CHOICES == *"FreeRTOS-Plus"* ]]; then
    cd iotc-freertos
    ACTUALHEADERS=( $(find -name "*.h") )
    cd ..

    for i in "${ACTUALHEADERS[@]}"
    do
        NAME=$(basename $i)

        if [ ! -e "$(find smart-inc/ -name $NAME)" ]; then
            ln -sr iotc-freertos/$i smart-inc/$NAME
        fi
    done
fi

symlinks -rc smart-inc 1&>/dev/null

# commented out the seds to see if patchfiles would be better, might change with FreeRTOS versions
#patch -u ./iotc-freertos/FreeRTOS-Plus/ThirdParty/mbedtls/include/mbedtls/mbedtls_config.h -i mbedtls_config.h.patch

#sed -i 's^//#define MBEDTLS_NO_PLATFORM_ENTROPY^#define MBEDTLS_NO_PLATFORM_ENTROPY^g' ./iotc-freertos/FreeRTOS-Plus/ThirdParty/mbedtls/include/mbedtls/mbedtls_config.h
#sed -i 's^#define MBEDTLS_NET_C^//#define MBEDTLS_NET_C^g' ./iotc-freertos/FreeRTOS-Plus/ThirdParty/mbedtls/include/mbedtls/mbedtls_config.h
#sed -i 's^#define MBEDTLS_TIMING_C^//#define MBEDTLS_TIMING_C^g' ./iotc-freertos/FreeRTOS-Plus/ThirdParty/mbedtls/include/mbedtls/mbedtls_config.h
#sed -i 's^#define MBEDTLS_FS_IO^//#define MBEDTLS_FS_IO^g' ./iotc-freertos/FreeRTOS-Plus/ThirdParty/mbedtls/include/mbedtls/mbedtls_config.h
#sed -i 's^#define MBEDTLS_PSA_ITS_FILE_C^//#define MBEDTLS_PSA_ITS_FILE_C^g' ./iotc-freertos/FreeRTOS-Plus/ThirdParty/mbedtls/include/mbedtls/mbedtls_config.h


# Return you to back where you execed the script
cd $current_dir

exit 0
