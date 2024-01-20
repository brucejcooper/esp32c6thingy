#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>

#include "openthread/cli.h"
#include "openthread/instance.h"
#include "esp_openthread.h"
#include <esp_openthread_cli.h>
#include "esp_openthread_types.h"
#include "esp_openthread_netif_glue.h"
#include "openthread/logging.h"
#include <openthread/platform/settings.h>
#include <openthread/ip6.h>
#include <openthread/dataset.h>
#include <openthread/netdata.h>
#include <openthread/srp_client.h>
#include <openthread/udp.h>
#include <openthread/thread.h>
#include <openthread/icmp6.h>
#include <openthread/tasklet.h>
#include <openthread/dataset_ftd.h>

#include "esp_vfs_eventfd.h"

#include "lua_system.h"

#define TAG "openthread"

/* The examples use simple Pre-Shared-Key configuration that you can set via
   'idf.py menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_COAP_PSK_KEY "some-agreed-preshared-key"

   Note: PSK will only be used if the URI is prefixed with coaps://
   instead of coap:// and the PSK must be one that the server supports
   (potentially associated with the IDENTITY)
*/
#define EXAMPLE_COAP_PSK_KEY CONFIG_EXAMPLE_COAP_PSK_KEY

/* The examples use CoAP Logging Level that
   you can set via 'idf.py menuconfig'.

   If you'd rather not, just change the below entry to a value
   that is between 0 and 7 with
   the config you want - ie #define EXAMPLE_COAP_LOG_DEFAULT_LEVEL 7
*/
#define EXAMPLE_COAP_LOG_DEFAULT_LEVEL CONFIG_COAP_LOG_DEFAULT_LEVEL

static esp_netif_t *init_openthread_netif(const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_openthread_netif_glue_init(config)));

    return netif;
}

void srp_server_callback(const otSockAddr *aServerSockAddr, void *aContext)
{
    char buf[40];
    otIp6AddressToString(&aServerSockAddr->mAddress, buf, sizeof(buf));
    ESP_LOGI(TAG, "Found SRP server %s:%d", buf, aServerSockAddr->mPort);
}

static int start_srp(lua_State *L)
{
    ESP_LOGI(TAG, "Enabling SRP Service registration");
    otInstance *instance = esp_openthread_get_instance();
    return 0;
}

static void register_srp(otNetifAddress *addr)
{
    ESP_LOGI(TAG, "Enabling SRP Service registration");
    otInstance *instance = esp_openthread_get_instance();

    // otSrpClientSetHostName(instance, defaultMacStr);
    // otSrpClientSetHostAddresses(instance, &addr->mAddress, 1);

    // static otSrpClientService service = {
    //     .mName = "_ccpeed._udp",    ///< The service labels (e.g., "_mt._udp", not the full domain name).
    //     .mInstanceName = defaultMacStr, ///< The service instance name label (not the full name).
    //     .mSubTypeLabels = NULL,     ///< Array of sub-type labels (must end with `NULL` or can be `NULL`).
    //     .mPort = OT_DEFAULT_COAP_PORT,
    //     .mPriority = 1,             ///< The service priority.
    //     .mWeight = 1,               ///< The service weight.
    //     .mNumTxtEntries = 0,        ///< Number of entries in the `mTxtEntries` array.
    //     .mTxtEntries = NULL,        ///< Array of TXT entries (`mNumTxtEntries` gives num of entries).
    //     .mLease = 0,                ///< Desired lease interval in sec - zero to use default.
    //     .mKeyLease = 0,             ///< Desired key lease interval in sec - zero to use default.
    // };
    // otSrpClientAddService(instance, &service);
    // otSrpClientEnableAutoStartMode(instance, srp_server_callback, NULL);
}

// Order is important here.  We want the first ones to be reported first.
static const code_lookup_t ipaddr_type_lookup[] = {
    {.ival = OT_ADDRESS_ORIGIN_MANUAL, .sval = "manual"},
    {.ival = OT_ADDRESS_ORIGIN_DHCPV6, .sval = "dhcpv6"},
    {.ival = OT_ADDRESS_ORIGIN_SLAAC, .sval = "slaac"},
    {.ival = OT_ADDRESS_ORIGIN_THREAD, .sval = "thread"},
    {.ival = -1, .sval = NULL}};

/**
 * Returns the list of IP addresses for the device, sorted in preference order.  i.e. the most public/manually set will be the first index.
 */
static int get_ipaddresses(lua_State *L)
{
    lua_newtable(L);
    int n = 1;

    otInstance *instance = esp_openthread_get_instance();
    otNetifAddress *addr = (otNetifAddress *)otIp6GetUnicastAddresses(instance);
    otNetifAddress *ptr;
    lua_newtable(L);

    for (code_lookup_t *cl = ipaddr_type_lookup; cl->sval; cl++)
    {
        ptr = addr;
        while (ptr)
        {
            if (addr->mAddressOrigin == cl->ival)
            {
                lua_pushinteger(L, n++);
                lua_newtable(L);
                lua_pushstring(L, "type");
                lua_pushstring(L, cl->sval);
                lua_settable(L, -3);

                lua_pushstring(L, "ip");
                lua_pushlstring(L, (const char *)ptr->mAddress.mFields.m8, OT_IP6_ADDRESS_SIZE);
                lua_settable(L, -3);

                lua_settable(L, -3);
            }
            ptr = (otNetifAddress *)ptr->mNext;
        }
    }
    return 1;
}

static void ipAddressChangeCallback(const otIp6AddressInfo *aAddressInfo, bool aIsAdded, void *aContext)
{
    char buf[OT_IP6_ADDRESS_STRING_SIZE];

    if (aIsAdded)
    {
        otInstance *instance = esp_openthread_get_instance();
        otNetifAddress *addr = (otNetifAddress *)otIp6GetUnicastAddresses(instance);

        while (addr)
        {
            if (addr->mAddressOrigin != OT_ADDRESS_ORIGIN_THREAD)
            {
                otIp6AddressToString(&(addr->mAddress), buf, OT_IP6_ADDRESS_STRING_SIZE);
                ESP_LOGI(TAG, "External IP Address %s/%d", buf, addr->mPrefixLength);

                if (!otSrpClientIsRunning(instance))
                {
                    register_srp(addr);
                }
            }
            addr = (otNetifAddress *)addr->mNext;
        }
    }
    else
    {
        ESP_LOGI(TAG, "IP Address Removed");
    }
}

static void ot_task_worker(void *aContext)
{
    otInstance *instance = esp_openthread_get_instance();
    otIp6SetAddressCallback(instance, ipAddressChangeCallback, NULL);

    // Enable responding to echo requests (ping)
    otIcmp6SetEchoMode(instance, OT_ICMP6_ECHO_HANDLER_ALL);

    // Run the main loop
    esp_err_t err = esp_openthread_launch_mainloop();
    ESP_LOGW(TAG, "Openthread mainloop exited.  This shouldn't normally happen: %s", esp_err_to_name(err));

    // Clean up
    esp_netif_destroy(esp_openthread_get_netif());
    esp_openthread_netif_glue_deinit();

    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

typedef enum
{
    VALUE_NOT_PRESENT,
    VALUE_VALID,
    VALUE_INVALID,
} set_result_t;

static set_result_t fetch_uint16_field(lua_State *L, int idx, char *name, uint16_t *out, bool *presenceOut)
{
    if (presenceOut)
    {
        *presenceOut = false;
    }
    lua_getfield(L, idx, name);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return VALUE_NOT_PRESENT;
    }
    if (!lua_isinteger(L, -1))
    {
        lua_pop(L, 1);
        luaL_error(L, "Invalid OpenThread field value %s", name);
        return VALUE_INVALID;
    }
    int val = lua_tointeger(L, -1);
    lua_pop(L, 1);

    if (val < 0 || val > 0xFFFF)
    {
        luaL_error(L, "Invalid OpenThread field value %s", name);
        return VALUE_INVALID;
    }
    *out = (uint16_t)val;
    if (presenceOut)
    {
        *presenceOut = true;
    }
    return VALUE_VALID;
}

static bool fetch_binary_field(lua_State *L, int idx, char *name, uint8_t *out, size_t expected_sz, bool *err)
{
    lua_getfield(L, idx, name);
    *err = true;
    if (lua_isstring(L, -1))
    {
        size_t len;
        const char *val = lua_tolstring(L, -1, &len);
        if (len == expected_sz)
        {
            memcpy(out, val, len);
            lua_pop(L, 1);
            *err = false;
            return true;
        }
    }
    else if (lua_isnil(L, -1))
    {
        *err = false;
    }
    lua_pop(L, 1);
    if (*err)
    {
        luaL_error(L, "Cannot convert OpenThread field %s", name);
    }
    return false;
}

static int set_active_dataset(lua_State *L)
{
    otError o_err;
    bool err;

    ESP_LOGD(TAG, "Setting active dataset");

    // We expect two parameters to be passed in - The openthread instance, and  A table with structured configuration, or alternatively a string with serialised configuration
    if (!lua_istable(L, 1))
    {
        luaL_argerror(L, 1, "Expected self");
    }
    if (!lua_istable(L, 2))
    {
        luaL_argerror(L, 2, "Expected parameters");
    }

    otInstance *instance = esp_openthread_get_instance();

    lua_pushstring(L, "dataset");
    otOperationalDataset *dataset = lua_newuserdata(L, sizeof(otOperationalDataset));

    otDatasetCreateNewNetwork(instance, dataset);
    // Active timestamp
    dataset->mActiveTimestamp.mSeconds = 1;
    dataset->mActiveTimestamp.mTicks = 0;
    dataset->mActiveTimestamp.mAuthoritative = false;
    dataset->mComponents.mIsActiveTimestampPresent = true;

    if (fetch_uint16_field(L, 2, "channel", &dataset->mChannel, &dataset->mComponents.mIsChannelPresent) == VALUE_INVALID)
    {
        ESP_LOGE(TAG, "Error setting channel");
        return 1;
    }
    switch (fetch_uint16_field(L, 2, "pan_id", &dataset->mPanId, &dataset->mComponents.mIsPanIdPresent))
    {
    case VALUE_VALID:
        ESP_LOGD(TAG, "pan_id set to 0x%02x", dataset->mPanId);
        break;
    case VALUE_NOT_PRESENT:
        ESP_LOGD(TAG, "No Pan Specified");
        break;
    case VALUE_INVALID:
        ESP_LOGE(TAG, "Error setting pan_id");
        return 1;
    }

    lua_getfield(L, 2, "network_name");
    if (lua_isstring(L, -1))
    {
        size_t sz;
        const char *network_name = lua_tolstring(L, -1, &sz);
        if (sz < sizeof(dataset->mNetworkName.m8) - 1)
        {
            strcpy((char *)dataset->mNetworkName.m8, network_name);
            dataset->mComponents.mIsNetworkNamePresent = true;
            ESP_LOGD(TAG, "Set network name to %s", network_name);
        }
        else
        {
            luaL_error(L, "network_name too long");
            return 1;
        }
    }
    lua_pop(L, 1);

    // Extended Pan ID
    dataset->mComponents.mIsExtendedPanIdPresent = fetch_binary_field(L, 2, "ext_pan_id", dataset->mExtendedPanId.m8, sizeof(dataset->mExtendedPanId.m8), &err);
    if (err)
    {
        ESP_LOGE(TAG, "Error setting netowrk ext_pan_id");
        return 1;
    }

    dataset->mComponents.mIsNetworkKeyPresent = fetch_binary_field(L, 2, "network_key", dataset->mNetworkKey.m8, sizeof(dataset->mNetworkKey.m8), &err);
    if (err)
    {
        ESP_LOGE(TAG, "Error setting netowrk key");
        return 1;
    }

    lua_getfield(L, 2, "mesh_local_prefix");
    if (lua_isstring(L, -1))
    {
        const char *prefixStr = lua_tostring(L, -1);
        otIp6Prefix prefix;
        if (otIp6PrefixFromString(prefixStr, &prefix) == OT_ERROR_NONE)
        {
            memcpy(dataset->mMeshLocalPrefix.m8, prefix.mPrefix.mFields.m8, sizeof(dataset->mMeshLocalPrefix.m8));
            dataset->mComponents.mIsMeshLocalPrefixPresent = true;
        }
        else
        {
            luaL_error(L, "Could not parse mesh_local_prefix");
            return 1;
        }
    }
    else if (!lua_isnil(L, -1))
    {
        luaL_error(L, "invalid mesh_local_prefix");
        return 1;
        // Error
    }

    dataset->mComponents.mIsPskcPresent = fetch_binary_field(L, 2, "provisioning_psk", dataset->mPskc.m8, sizeof(dataset->mPskc.m8), &err);
    if (err)
    {
        ESP_LOGE(TAG, "Error setting psk");
        return 1;
    }

    if ((o_err = otDatasetSetActive(instance, dataset)) != OT_ERROR_NONE)
    {
        luaL_error(L, "Failed to set OpenThread active dataset: %s", otThreadErrorToString(o_err));
        return 1;
    }

    lua_settable(L, 1);
    return 0;
}

static int ot_start(lua_State *L)
{
    // Expects one arg, the object.
    otError o_err;

    if (!lua_istable(L, 1))
    {
        luaL_argerror(L, 1, "Expected self");
        return 1;
    }

    otInstance *instance = esp_openthread_get_instance();

    if ((o_err = otIp6SetEnabled(instance, true) != OT_ERROR_NONE))
    {
        luaL_error(L, "Failed to enable OpenThread IPv6 interface");
        return 1;
    }
    if ((o_err = otThreadSetEnabled(instance, true) != OT_ERROR_NONE))
    {
        luaL_error(L, "Failed to enable OpenThread");
        return 1;
    }
    ESP_LOGI(TAG, "Started OpenThread");
    xTaskCreate(ot_task_worker, "ot_loop", 10240, xTaskGetCurrentTaskHandle(), 5, NULL);
    return 0;
}

static int new_ot(lua_State *L)
{
    esp_err_t e_err;

    if (!(lua_istable(L, 1)))
    {
        luaL_argerror(L, 1, "Expected to be called with OpenThread as self");
        return 1;
    }
    int nArgs = lua_gettop(L);

    lua_newtable(L);

    lua_pushvalue(L, 1);
    lua_setmetatable(L, -2); // Set the metadata table to the OpenThread global itself (assuming this method was called with it)

    lua_pushstring(L, "_config");
    esp_openthread_platform_config_t *config = lua_newuserdata(L, sizeof(esp_openthread_platform_config_t));
    config->radio_config.radio_mode = RADIO_MODE_NATIVE;
    config->host_config.host_connection_mode = HOST_CONNECTION_MODE_CLI_UART;
    config->host_config.host_uart_config.port = 0;
    config->host_config.host_uart_config.uart_config.baud_rate = 115200;
    config->host_config.host_uart_config.uart_config.data_bits = UART_DATA_8_BITS;
    config->host_config.host_uart_config.uart_config.parity = UART_PARITY_DISABLE;
    config->host_config.host_uart_config.uart_config.stop_bits = UART_STOP_BITS_1;
    config->host_config.host_uart_config.uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config->host_config.host_uart_config.uart_config.rx_flow_ctrl_thresh = 0;
    config->host_config.host_uart_config.uart_config.source_clk = UART_SCLK_DEFAULT;
    config->host_config.host_uart_config.rx_pin = UART_PIN_NO_CHANGE;
    config->host_config.host_uart_config.tx_pin = UART_PIN_NO_CHANGE;
    config->port_config.storage_partition_name = "nvs";
    config->port_config.netif_queue_size = 10;
    config->port_config.task_queue_size = 10;
    lua_settable(L, -3);

    if ((e_err = esp_openthread_init(config)) != ESP_OK)
    {
        luaL_error(L, "Could not initialise OpenThread: %s", esp_err_to_name(e_err));
        return 1;
    }

    // The OpenThread log level directly matches ESP log level
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);

    esp_netif_t *openthread_netif;
    // Initialize the esp_netif bindings
    openthread_netif = init_openthread_netif(config);
    esp_netif_set_default_netif(openthread_netif);

    // Set the dataset
    if (nArgs > 1)
    {
        // Convenience to pass through an initial active dataset to the constructor.
        lua_pushcfunction(L, set_active_dataset);
        lua_pushvalue(L, -2); // the table we just created
        lua_pushvalue(L, 2);  // The argument
        lua_call(L, 2, 0);
    }
    return 1;
}

typedef struct
{
    otSockAddr sockAddr;
    otUdpSocket sock;
    int objRef;
} udp_listener_t;

static int close_udp(lua_State *L)
{
    lua_getfield(L, 1, "sock");
    udp_listener_t *l = (udp_listener_t *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    otInstance *instance = esp_openthread_get_instance();

    otError otErr = otUdpClose(instance, &l->sock);
    if (otErr != OT_ERROR_NONE)
    {
        luaL_error(L, "Error closing socket: %s", otThreadErrorToString(otErr));
        return 1;
    }
    luaL_unref(L, LUA_REGISTRYINDEX, l->objRef);
    return 1;
}

static int send_datagram(lua_State *L)
{
    // Arg 1 is the source IP.
    // Arg 2 is the source port.
    // Arg 3 is the peer IP.
    // Arg 4 is the peer port.
    // Arg 5 is the string body we wish to transmit.

    if (lua_gettop(L) != 5)
    {
        luaL_error(L, "Expected 5 arguments: myip, myport, peerip, peerport, data");
        return 1;
    }

    size_t sz;

    otMessageInfo msgInfo = {
        .mLinkInfo = NULL,
        .mAllowZeroHopLimit = false,
        .mEcn = 0,
        .mHopLimit = 63,
        .mIsHostInterface = 0,
        .mMulticastLoop = 0,
    };

    const char *sock_addr = lua_tolstring(L, 1, &sz);
    if (sz != OT_IP6_ADDRESS_SIZE)
    {
        luaL_argerror(L, 1, "peer_addr must be 16 bytes long");
        return 1;
    }
    memcpy(msgInfo.mSockAddr.mFields.m8, sock_addr, sz);
    msgInfo.mSockPort = lua_tointeger(L, 2);

    const char *peer_addr = lua_tolstring(L, 3, &sz);
    if (sz != OT_IP6_ADDRESS_SIZE)
    {
        luaL_argerror(L, 3, "peer_addr must be 16 bytes long");
        return 1;
    }
    memcpy(msgInfo.mPeerAddr.mFields.m8, peer_addr, sz);
    msgInfo.mPeerPort = lua_tointeger(L, 4);

    const char *msg = lua_tolstring(L, 5, &sz);
    char buf1[OT_IP6_ADDRESS_STRING_SIZE];
    char buf2[OT_IP6_ADDRESS_STRING_SIZE];

    if (esp_log_level_get(TAG) >= ESP_LOG_DEBUG)
    {
        otIp6AddressToString(&msgInfo.mPeerAddr, buf1, sizeof(buf1));
        otIp6AddressToString(&msgInfo.mSockAddr, buf2, sizeof(buf2));
        ESP_LOGD(TAG, "Sending Datagram from %s:%d -> %s:%d length %d", buf2, msgInfo.mSockPort, buf1, msgInfo.mPeerPort, sz);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, msg, sz, ESP_LOG_DEBUG);
    }

    otInstance *instance = esp_openthread_get_instance();
    otMessage *respMsg = otUdpNewMessage(instance, NULL);
    otMessageAppend(respMsg, (uint8_t *)msg, sz);

    otError oErr = otUdpSendDatagram(instance, respMsg, &msgInfo);
    if (oErr != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "could not send datagram");
        otMessageFree(respMsg);
        luaL_error(L, "Error sending UDP datagram: %s", otThreadErrorToString(oErr));
        return 1;
    }
    return 0;
}

void udpCallback(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    udp_listener_t *sock = (udp_listener_t *)aContext;
    uint8_t buf[1500]; // Bigger than the MTU
    size_t bufsz;

    ESP_LOGD(TAG, "Received UDP Datagram. AllowZeroHopLimit %d, ecn %d, hopLimit %d, isHostInterface %d, linkInfo %p multicastLoop %d",
             aMessageInfo->mAllowZeroHopLimit,
             aMessageInfo->mEcn,
             aMessageInfo->mHopLimit,
             aMessageInfo->mIsHostInterface,
             aMessageInfo->mLinkInfo,
             aMessageInfo->mMulticastLoop);

    lua_State *L = acquireLuaMutex();
    // Get the serverSocket object
    assert(lua_rawgeti(L, LUA_REGISTRYINDEX, sock->objRef));

    // Get the handler from the object.
    lua_getfield(L, -1, "handler");
    lua_pushvalue(L, -2); // First argument is the The "sock" object

    // Second argument is a request object with information about the message received.
    lua_newtable(L);
    lua_pushstring(L, "body");
    bufsz = otMessageRead(aMessage, 0, buf, sizeof(buf));

    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, bufsz, ESP_LOG_DEBUG);

    lua_pushlstring(L, (char *)buf, bufsz);
    lua_settable(L, -3);

    lua_pushstring(L, "peer_addr");
    lua_pushlstring(L, (const char *)aMessageInfo->mPeerAddr.mFields.m8, OT_IP6_ADDRESS_SIZE);
    lua_settable(L, -3);

    lua_pushstring(L, "peer_port");
    lua_pushinteger(L, aMessageInfo->mPeerPort);
    lua_settable(L, -3);

    lua_pushstring(L, "sock_addr");
    lua_pushlstring(L, (const char *)aMessageInfo->mSockAddr.mFields.m8, OT_IP6_ADDRESS_SIZE);
    lua_settable(L, -3);

    lua_pushstring(L, "sock_port");
    lua_pushinteger(L, aMessageInfo->mSockPort);
    lua_settable(L, -3);

    if (lua_pcall(L, 2, 0, 0))
    {
        ESP_LOGE(TAG, "Error processing UDP Packet on port %d: %s", aMessageInfo->mSockPort, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1); // The object that we fetched first up

    releaseLuaMutex();
}

static int listen_udp(lua_State *L)
{
    // Arg 1 is self (the openthread instance)
    // arg 2 is port
    // arg 3 is handler
    // Returns a table that represents a "UpdServerSocket" - The handler may be modified, or it may be closed.

    lua_newtable(L);
    lua_pushstring(L, "sock");
    udp_listener_t *udpListener = lua_newuserdata(L, sizeof(udp_listener_t));
    lua_settable(L, -3);

    lua_pushstring(L, "close");
    lua_pushcfunction(L, close_udp);
    lua_settable(L, -3);

    lua_pushstring(L, "handler");
    lua_pushvalue(L, 3);
    lua_settable(L, -3);

    memset(&udpListener->sockAddr, 0, sizeof(otSockAddr)); // Unspecified (every address)
    udpListener->sockAddr.mPort = lua_tointeger(L, 2);

    otInstance *instance = esp_openthread_get_instance();
    otError error = otUdpOpen(instance, &udpListener->sock, udpCallback, udpListener);
    if (error != OT_ERROR_NONE)
    {
        luaL_error(L, "Error opening UDP");
        return 1;
    }
    else
    {
        error = otUdpBind(instance, &udpListener->sock, &udpListener->sockAddr, OT_NETIF_THREAD);
        if (error != OT_ERROR_NONE)
        {
            luaL_error(L, "Error binding UDP");
            return 1;
        }
    }
    // Take a reference to the object so that it doesn't get garbage collected.
    lua_pushvalue(L, -1);
    udpListener->objRef = luaL_ref(L, LUA_REGISTRYINDEX);

    ESP_LOGI(TAG, "Created UDP listener on port %d", udpListener->sockAddr.mPort);

    return 1;
}

static int str_to_ip6(lua_State *L)
{
    if (!lua_isstring(L, 1))
    {
        luaL_argerror(L, 1, "addr must be a 16 byte string");
    }
    const char *str = lua_tostring(L, 1);

    otIp6Address addr;
    otError err = otIp6AddressFromString(str, &addr);
    if (err != OT_ERROR_NONE)
    {
        luaL_error(L, otThreadErrorToString(err));
        return 1;
    }
    lua_pushlstring(L, (char *)addr.mFields.m8, OT_IP6_ADDRESS_SIZE);
    return 1;
}

static int ip6_to_str(lua_State *L)
{
    size_t sz;
    char buf[OT_IP6_ADDRESS_STRING_SIZE];

    if (!lua_isstring(L, 1))
    {
        luaL_argerror(L, 1, "addr must be a 16 byte string");
    }
    const otIp6Address *aAddress = (const otIp6Address *)lua_tolstring(L, 1, &sz);
    if (sz != OT_IP6_ADDRESS_SIZE)
    {
        luaL_argerror(L, 1, "addr must be a 16 byte string");
    }
    otIp6AddressToString(aAddress, buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}

static const struct luaL_Reg funcs[] = {
    {"init", new_ot},
    {"set_active_dataset", set_active_dataset},
    {"send_dgram", send_datagram},
    {"listen_udp", listen_udp},
    {"start", ot_start},
    {"ip_addresses", get_ipaddresses},
    {"ip6_to_str", ip6_to_str},
    {"parse_ip6", str_to_ip6},
    {NULL, NULL}};

int luaopen_openthread(lua_State *L)
{
    luaL_newlib(L, funcs);
    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);
    return 1;
}