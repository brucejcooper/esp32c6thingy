#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <esp_log.h>
#include "lua_system.h"

#define TAG "gpio"

typedef struct {
    int pin;
    int handlerRef;
    int argRef;
    gpio_int_type_t type;
    bool pending;
} interrupt_handler_info_t;

static interrupt_handler_info_t interrupt_handler_refs[32];


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
    
    LUA_ESP_ERR_CHECK(gpio_config(&gpioConfig));
    return 0;
}


static const code_lookup_t interrupt_type_lookup[] = {
    { .sval="disable", .ival=GPIO_INTR_DISABLE},
    { .sval="positive", .ival=GPIO_INTR_POSEDGE},
    { .sval="negative", .ival=GPIO_INTR_NEGEDGE},
    { .sval="any_edge", .ival=GPIO_INTR_ANYEDGE},
    { .sval="low", .ival=GPIO_INTR_LOW_LEVEL},
    { .sval="high", .ival=GPIO_INTR_HIGH_LEVEL},
    {.sval=NULL, .ival=-1}
};


void do_callback(lua_State *L, void *arg) {
    interrupt_handler_info_t *ih = (interrupt_handler_info_t *) arg;
    // ESP_EARLY_LOGI(TAG, "cb%d %d", ih->pin, ih->type);

    // We don't want any more interrupts until the 
    if (ih->type != GPIO_INTR_DISABLE) {
        assert(lua_rawgeti(L, LUA_REGISTRYINDEX, ih->handlerRef));
        assert(lua_rawgeti(L, LUA_REGISTRYINDEX, ih->argRef));
        if (lua_pcall(L, 1, 0, 0)) {
            ESP_LOGE(TAG, "Error calling GPIO callback: %s", lua_tostring(L, 1));
            lua_pop(L, 1);
        }
        // If interrupts are still enabled, turn them back on. 
        if (ih->pending > 0) {
            ih->pending = false;
            gpio_intr_enable(ih->pin);
        }
    }
}


void gpio_isr(void *arg) {
    interrupt_handler_info_t *ih = (interrupt_handler_info_t *) arg;
    // ESP_EARLY_LOGI(TAG, "isr%d %d", ih->pin, ih->type);
    if (ih->pending) {
        ESP_EARLY_LOGW(TAG, "GPIO isr while one already pending");
        return;
    }
    ih->pending = true;
    gpio_intr_disable(ih->pin); 
    schedule_callback_from_ISR(do_callback, arg);
    // For things like level interrupts, this would immediately re-raise, before the handler
    // function has been completed.  to deal with this, we turn them off until the handler completes.

}


/**
 * Arg 1 is pin
 * Arg 2 is handler - or nill to disable (in which case it ignores arg 3)
 * Arg 3 is intr type - if its a number that is the expected level.  If it is "positive", "negative" or "both" then use. 
*/
static int set_pin_isr(lua_State *L) {
    int isnum;
    int pin = lua_tointegerx(L, 1, &isnum);
    if (!isnum || pin < 0 || pin > 31) {
        luaL_argerror(L, 1, "Arg 1 must be an integer between 0 and 31");
        return 1;
    }

    gpio_int_type_t inttype = lua_lookup(L, 2, interrupt_type_lookup);
    if (inttype == -1) {
        if (lua_isnil(L, 2)) {
            inttype = GPIO_INTR_DISABLE;
        } else {
            if (lua_isstring(L, 2)) {
                ESP_LOGW(TAG, "Couldn't map %s to interrupt type", lua_tostring(L, 2));
            } else if (lua_isinteger(L, 4)) {
                ESP_LOGW(TAG, "Couldn't map %lld to interrupt type", lua_tointeger(L, 2));
            } else {
                ESP_LOGW(TAG, "Invalid interrupt type %s", lua_type_str(L, 2));
            }
            luaL_argerror(L, 2, "Invalid interrupt type");
            return 1;
        }
    }

    if (lua_isfunction(L, 3)) {
        // third argument is a context object to pass through
        // fourth argument is the level. 
    } else if (!(inttype == GPIO_INTR_DISABLE && lua_gettop(L) == 2)) {
        luaL_argerror(L, 3, "Incorrect arguments - allows pin, \"disable\" or pin, level, fn, <arg>");
        return 1;
    }

    interrupt_handler_info_t *ih = interrupt_handler_refs + pin;
    if (ih->type != GPIO_INTR_DISABLE)  {
        LUA_ESP_ERR_CHECK(gpio_intr_disable(pin));
        LUA_ESP_ERR_CHECK(gpio_isr_handler_remove(pin));
        luaL_unref(L, LUA_REGISTRYINDEX, ih->argRef);
        luaL_unref(L, LUA_REGISTRYINDEX, ih->handlerRef);
        ih->pending = false;
    }

    ih->type = inttype;
    lua_pushvalue(L, 3);
    ih->handlerRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 4);
    ih->argRef = luaL_ref(L, LUA_REGISTRYINDEX);
    ih->pending = false;

    LUA_ESP_ERR_CHECK(gpio_set_intr_type(pin, inttype));
    if (inttype != GPIO_INTR_DISABLE) {
        LUA_ESP_ERR_CHECK(gpio_isr_handler_add(pin, gpio_isr, ih));
        LUA_ESP_ERR_CHECK(gpio_intr_enable(pin));
    }

    return 0;
}


static const struct luaL_Reg log_funcs[] = {
    { "get", get },
    { "config_input", config_input },
    { "set_pin_isr", set_pin_isr },
    { NULL, NULL }
};

int luaopen_gpio(lua_State *L)
{
    interrupt_handler_info_t *ii = interrupt_handler_refs;
    for (int i = 0; i < 32; i++, ii++) {
        ii->pin = i;
        ii->argRef = LUA_NOREF;
        ii->handlerRef = LUA_NOREF;
        ii->type = GPIO_INTR_DISABLE;
        ii->pending = false;
    }
    luaL_newlib(L, log_funcs);
    esp_err_t err = gpio_install_isr_service(0);
    assert(err == ESP_OK || err == ESP_ERR_INVALID_STATE);

    return 1;
}