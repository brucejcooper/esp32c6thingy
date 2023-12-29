#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>

#define TAG "gpio"

static int get(lua_State *L){
    // We expect one parameter to be passed in - a Table. 
    int pin = luaL_checkinteger(L, 1);
    int level = gpio_get_level(pin);
    lua_pushinteger(L, level);
    return 1;
}

static int config_input(lua_State *L) {
    // We expect one parameter to be passed in - a Table. 
    int pin = luaL_checkinteger(L, 1);

    ESP_LOGI(TAG, "Configuring pin %d as an input", pin);

    gpio_config_t gpioConfig = {
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE, // we start out with no interrupts. 
        .pin_bit_mask = (1 << pin),
    };
    ESP_ERROR_CHECK(gpio_config(&gpioConfig));
    // TODO return error on failure, instead of panicing.
    return 0;
}


static const struct luaL_Reg device_funcs[] = {
    { "get", get },
    { "config_input", config_input },
    { NULL, NULL }
};

int luaopen_gpio(lua_State *L)
{
    luaL_newlib(L, device_funcs);
    esp_err_t err = gpio_install_isr_service(0);
    assert(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

    return 1;
}