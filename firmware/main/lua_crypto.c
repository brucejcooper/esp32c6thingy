#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include "dali_driver.h"
#include "lua_dali.h"
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <openthread/crypto.h>

#define TAG "dali"

static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_entropy_context entropy;
static mbedtls_ecp_group secp256r1_grp;
static const char *personalization = "WhaddAboutAWaddaBoddleBiddle";

static int generate_random(lua_State *L)
{
    unsigned char out[128]; // 128 bytes is a lot of bits.
    int isNum;
    int sz = lua_tointegerx(L, 1, &isNum);
    if (!isNum || sz <= 0 || sz > sizeof(out))
    {
        luaL_argerror(L, 1, "Expected size parameter");
    }

    if (mbedtls_ctr_drbg_random(&ctr_drbg, out, sz))
    {
        luaL_error(L, "Could not generate random data");
        return 1;
    }

    lua_pushlstring(L, (char *)out, sz);
    return 0;
}

static int ec_keypair_gen(lua_State *L)
{
    mbedtls_mpi private;
    mbedtls_ecp_point public;
    uint8_t buf[200];
    size_t olen;

    ESP_LOGI(TAG, "Creating Keypair");

    const mbedtls_ecp_curve_info *curve_info = mbedtls_ecp_curve_info_from_grp_id(MBEDTLS_ECP_DP_SECP256R1);
    size_t keySz = curve_info->bit_size / 8;

    ESP_LOGI(TAG, "Key size is %d", keySz);

    mbedtls_mpi_init(&private);
    mbedtls_ecp_point_init(&public);

    if (mbedtls_ecdh_gen_public(&secp256r1_grp, &private, &public, mbedtls_ctr_drbg_random, &ctr_drbg) != 0)
    {
        luaL_error(L, "Error generating keypair");
        return 1;
    }

    ESP_LOGI(TAG, "Writing private");

    if (mbedtls_mpi_write_binary(&private, buf, keySz) != 0)
    {
        luaL_error(L, "Could not write private key");
        return 1;
    }
    lua_pushlstring(L, (char *)buf, keySz);

    ESP_LOGI(TAG, "Writing public");

    if (mbedtls_ecp_tls_write_point(&secp256r1_grp, &public, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, buf, sizeof(buf)) != 0)
    {
        luaL_error(L, "Could not write public key");
        return 1;
    }
    lua_pushlstring(L, (char *)buf, olen);

    mbedtls_mpi_free(&private);
    mbedtls_ecp_point_free(&public);

    ESP_LOGI(TAG, "Done");

    return 2;
}

static const struct luaL_Reg funcs[] = {
    {"random", generate_random},
    {"ec_keypair_gen", ec_keypair_gen},
    {NULL, NULL}};

int luaopen_crypto(lua_State *L)
{
    luaL_newlib(L, funcs);

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                          (const unsigned char *)personalization,
                          strlen((personalization)));

    mbedtls_ecp_group_load(&secp256r1_grp, MBEDTLS_ECP_DP_SECP256R1);
    return 1;
}