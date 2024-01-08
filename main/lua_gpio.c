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
    
    esp_err_t err = gpio_config(&gpioConfig);
    if (err != ESP_OK) {
        luaL_error(L, "Could not configure gpio pin err code 0x%02x", err);
        return 1;
    }
    return 0;
}

static int cancel_await_level(lua_State *L) {
    return 0;
}

static int await_level(lua_State *L) {
    int isnum;
    int pin = lua_tointegerx(L, 1, &isnum);
    if (!isnum || pin < 0 || pin > 32) {
        luaL_argerror(L, 1, "Expected an int within the normal pin range");
        return 1;
    }

    // Create a future that we will call once the interrupt fires
    lua_getglobal(L, "Future");
    lua_getfield(L, -1, "new");
    lua_newtable(L); // The argument to new
    lua_pushstring(L, "cancel");
    lua_pushcfunction(L, cancel_await_level);
    lua_settable(L, -3);

    lua_call(L, 1, 1);
    return 1;
}


static const struct luaL_Reg log_funcs[] = {
    { "get", get },
    { "config_input", config_input },
    { "await_level", await_level },
    { NULL, NULL }
};

int luaopen_gpio(lua_State *L)
{
    luaL_newlib(L, log_funcs);
    esp_err_t err = gpio_install_isr_service(0);
    assert(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

    return 1;
}