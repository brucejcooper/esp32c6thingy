#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <string.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <stdint.h>
#include "dali_driver.h"

#include "lua_system.h"
#include "lua_gpio.h"
#include "lua_coap.h"
#include "lua_log.h"
#include "lua_dali.h"



#define TAG "lua_system"

static int await(lua_State *L) {
    // Expects a single table input, with the appropriate values, but fundamentally it just passes this to yield.
    return lua_yield(L, 1);
}


typedef struct {
    int pin;
    uint32_t level;
    uint32_t timeout;
} input_task_t;


typedef struct  {
    int task_ref;
    esp_timer_handle_t timer;
    int ret;

    // If we're waiting for GPIO, this will be set to the pin that we are waiting on. Will be set to -1 if not waiting on a pin
    int gpio_waiting_pin;
    int gpio_expected_lvl;

    bool dali_cmd_active;

} coro_entry_t;


#define MAX_COROS 10
static int num_coros = 0;
static coro_entry_t coros[MAX_COROS];

static QueueHandle_t queue;


static void timer_callback(void *arg);


static void schedule_coro(coro_entry_t *coro) {
    // This can't block, because that would starve the thread that is also draining the queue
    ESP_LOGD(TAG, "CORO %d scheduling", coro->task_ref);
    if (xQueueSend(queue, &coro, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Error enqueueing coro to run");
    }
}


static int start_coro(lua_State *L) {
    if (!lua_isfunction(L, 1)) {
        luaL_argerror(L, 1, "Expected a function");
        return 1;
    }
    // Store the coroutine as a reference in the registry.
    if (num_coros >= MAX_COROS) {
        luaL_error(L, "Too many co-routines");
        return 1;
    }
    // Start a co-routine with the function argument as the handler. 
    lua_getglobal(L, "coroutine");
    lua_getfield(L, -1, "create");
    lua_pushnil(L);
    lua_copy(L, 1, -1);
    int r = lua_pcall(L, 1, 1, 0);
    if (r) {
        luaL_error(L, "Could not create coroutine");
        return 1;
    }

    // The top of the stack will now be the thread/co-routine..
    if (!lua_isthread(L, -1)) {
        luaL_argerror(L, 1, "After starting coroutine, it wasn't on the top of the stack");
        return 1;
    }
    // Take a reference to it.
    coro_entry_t *coro = &coros[num_coros++];
    coro->task_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    coro->ret = 0;
    coro->gpio_waiting_pin = -1;
    coro->dali_cmd_active = false;

    // Each coro gets its own high res timer, whether it uses it or not. 
    esp_timer_create_args_t args = {
        .arg = coro,
        .callback = timer_callback,
        .dispatch_method = ESP_TIMER_ISR,
    };
    // Create a new one shot timer to wait for the supplied resperiod. 
    ESP_ERROR_CHECK(esp_timer_create(&args, &coro->timer));
    schedule_coro(coro);

    ESP_LOGI(TAG, "Registered coroutine with id %d", coro->task_ref);
    return 1;
}

static int restart_system(lua_State *L) {
    esp_restart();
    return 0;
}

static const struct luaL_Reg eventloop_funcs[] = {
    { "await", await },
    { "start_coro", start_coro },
    { "restart", restart_system },
    { NULL, NULL }
};


void lua_report_error(lua_State *L, int status, const char *prefix) {
    if (status == LUA_OK)
        return;

    const char *msg = lua_tostring(L, -1);
    ESP_LOGE(TAG, "%s: %s", prefix, msg);
    lua_pop(L, 1);
}


static void timer_callback(void *arg) {
    coro_entry_t *coro = arg;
    BaseType_t higherPriorityTaskWoken;

    ESP_EARLY_LOGD(TAG, "Timer on task %d completed", coro->task_ref);
    coro->dali_cmd_active = false;
    coro->ret = -1;
    xQueueSendFromISR(queue, &coro, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken) {
        taskYIELD();
    }
}

static void gpio_intr(void *arg) {
    BaseType_t higherPriorityTaskWoken;
    coro_entry_t *coro = arg;
    // Cancel any timer (if running);
    esp_timer_stop(coro->timer);
    coro->dali_cmd_active = false;

    if (coro->gpio_waiting_pin > 0) {
        ESP_EARLY_LOGD(TAG, "GPIO interrupt for task %d GPIO %d", coro->task_ref, coro->gpio_waiting_pin);
        // Disable the interrupt. 
        gpio_intr_disable(coro->gpio_waiting_pin);
        gpio_isr_handler_remove(coro->gpio_waiting_pin);
        gpio_set_intr_type(coro->gpio_waiting_pin, GPIO_INTR_DISABLE);
        coro->gpio_waiting_pin = -1;

        // Schedule the coro as runnable. the return value of 0 indicates that the GPIO level was reached, as opposed for -1 for timeout
        coro->ret = 0;
        xQueueSendFromISR(queue, &coro, &higherPriorityTaskWoken);
        if (higherPriorityTaskWoken) {
            taskYIELD();
        }
    }
}


static void dali_command_callback(int response, void *arg) {
    BaseType_t higherPriorityTaskWoken;
    coro_entry_t *coro = arg;

    if (coro->dali_cmd_active) {
        coro->dali_cmd_active = false;
        coro->ret = response;
        xQueueSendFromISR(queue, &coro, &higherPriorityTaskWoken);
        if (higherPriorityTaskWoken) {
            taskYIELD();
        }
    }
}



static void delete_coro(lua_State *L, coro_entry_t *coro) {
    int i = coros - coro;

    ESP_ERROR_CHECK(esp_timer_delete(coro->timer));
    if (coro->gpio_waiting_pin != -1) {
        gpio_intr_disable(coro->gpio_waiting_pin);
        gpio_isr_handler_remove(coro->gpio_waiting_pin);
        gpio_set_intr_type(coro->gpio_waiting_pin, GPIO_INTR_DISABLE);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, coro->task_ref);
    coro->dali_cmd_active = false;

    num_coros--;
    if (num_coros > 0) {
        memcpy(coros, coros+1, (num_coros-i)*sizeof(coro_entry_t));
    }
}


bool get_int(lua_State *L, const char *fname, int *out, int default_value) {
    bool res = false;
    lua_getfield(L, -1, fname);
    if (lua_isinteger(L, -1)) {
        *out = lua_tointeger(L, -1);
        res = true;
    } else if (lua_isnil(L, -1)) {
        *out = default_value;
        res = true;
    } else {
        ESP_LOGE(TAG, "Invalid field type");
    }
    lua_pop(L, 1);
    return res;
}


static const char *type_names[] = {
    "nil",
    "boolean",
    "light_user_data",
    "number",
    "string",
    "table",
    "function",
    "user_data",
    "thread",
};


void dumpStack(lua_State *L) {
    for (int i = lua_gettop(L); i > 0; i--) {
        switch (lua_type(L, i)) {
            case LUA_TBOOLEAN:
                ESP_LOGI(TAG, "[%d] %s", i, lua_toboolean(L, i) ? "true" : "false");
            break;
            case LUA_TSTRING:
                ESP_LOGI(TAG, "[%d] \"%s\"", i, lua_tostring(L, i));
            break;
            default:
                ESP_LOGI(TAG, "[%d] %s %p", i, type_names[lua_type(L, i)], lua_topointer(L, i));
                break;
        }
    }

}


static void run_coro(lua_State *L, coro_entry_t *coro) {
    int delay, pin, level, dali_cmd;

    ESP_LOGD(TAG, "Running Coro %d - top is %d", coro->task_ref, lua_gettop(L));

    // Get global "coroutine"
    assert(lua_getglobal(L, "coroutine"));
    // Find field (function) "resume"
    assert(lua_getfield(L, -1, "resume"));

    // Run the method with two arguments (the coro and the ret value from the last event), expecting two responses (although the second one might be nil)
    assert(lua_rawgeti(L, LUA_REGISTRYINDEX, coro->task_ref));
    lua_pushinteger(L, coro->ret);
    int r = lua_pcall(L, 2, 2, 0);
    if (r) {
        lua_report_error(L, r, "Coroutine call failure");
    }
    ESP_LOGD(TAG, "CORO %d returned %d %s", coro->task_ref, lua_toboolean(L, -2), type_names[lua_type(L, -1)]);
    // Get the response (which should have two responses - whether its finished and any arguments - in the event of an error this will be the second argument)
    bool still_running = false;
    if (!lua_isboolean(L, -2)) {
        ESP_LOGE(TAG, "First response should be a boolean");
    }
    still_running = lua_toboolean(L, -2);
    if (still_running) {
        if (lua_istable(L, -1)) {
            // An await may include a timeout
            if (get_int(L, "timeout", &delay, -1)) {
                if (delay > 0) {
                    ESP_LOGD(TAG, "Timeout in %d ms", delay);
                    ESP_ERROR_CHECK(esp_timer_start_once(coro->timer, delay*1000));
                }
            } else {
                ESP_LOGE(TAG, "Invalid timeout");
            }

            // If pin and level is specified, then it will also resume once that GPIO is reached.
            if (get_int(L, "pin", &pin, -1) && get_int(L, "level", &level, -1)) {
                if (pin != -1 && level != -1) {
                    ESP_LOGD(TAG, "Waiting for GPIO level %d on pin %d for up to %d ms", level, pin, delay);
                    // Turn on an interrupt that will go off when the GPIO is at the speicified level 
                    coro->gpio_waiting_pin = pin;
                    gpio_isr_handler_add(pin, gpio_intr, coro);
                    gpio_set_intr_type(pin, level == 1 ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
                    gpio_intr_enable(pin);
                }
            } else {
                ESP_LOGE(TAG, "Invalid delay");
            }

            
            if (get_int(L, "dali", &dali_cmd, -1)) {
                if (dali_cmd != -1) {
                    lua_getfield(L, -1, "driver");
                    if (lua_islightuserdata(L, -1)) {
                        dali_driver_t *driver = (dali_driver_t *) lua_touserdata(L, -1);
                        ESP_LOGD(TAG, "Sending command to Driver %p with tx %lu and rx %lu", driver, driver->tx_pin, driver->rx_pin);
                        dali_send_command(driver, dali_cmd, dali_command_callback, coro);
                        coro->dali_cmd_active = true;
                    } else {
                        ESP_LOGE(TAG, "No or incorrect driver supplied");
                    }
                    lua_pop(L, 1); // THe driver value.
                }
            } else {
                ESP_LOGE(TAG, "Invalid dali command");
            }
        } else if (!lua_isnil(L, -1)) {
            ESP_LOGW(TAG, "Incorrect response type from yield.  This may leak, as nothing will resume it");
        }
    } else {
        // If the second arg is a string, then an error has occurred. 
        if (lua_isstring(L, -1)) {
            const char *msg = lua_tostring(L, -1);
            ESP_LOGE(TAG, "Error running co-routine %d: %s", coro->task_ref, msg);

        }    
    }
    lua_pop(L, 2); // Dispose of the 2 responses


    // the coroutine global is still present at the top of the stack. Use it to get the status of the co-routine.
    lua_getfield(L, -1, "status");
    assert(lua_rawgeti(L, LUA_REGISTRYINDEX, coro->task_ref)); // Parameter
    r = lua_pcall(L, 1, 1, 0);
    if (r) {
        lua_report_error(L, r, "Can not get coroutine status");
    } else {
        const char *statusstr = lua_tostring(L, -1);
        if (strcmp(statusstr, "dead") == 0) {
            ESP_LOGI(TAG, "CORO %d has completed", coro->task_ref);
            delete_coro(L, coro);
        } else if (strcmp(statusstr, "suspended") == 0) {
            ESP_LOGV(TAG, "CORO %d suspended", coro->task_ref);
        } else {
            ESP_LOGW(TAG, "CORO %d in unstable state %s", coro->task_ref, statusstr);
        }
        lua_pop(L, 1); // The response.
    }
    lua_pop(L, 1); // The coroutine global


    // When we get here, the stack should be back to zero
    assert(lua_gettop(L) == 0); // TODO just nuke the stack for safety?



}





int luaopen_system(lua_State *L) {
    luaL_newlib(L, eventloop_funcs);
    return 1;
}

static void load_custom_libs(lua_State *L) {
    
    luaL_requiref(L, "system", luaopen_system, true);
    lua_pop(L, 1);
    luaL_requiref(L, "gpio", luaopen_gpio, true);
    lua_pop(L, 1);
    luaL_requiref(L, "coap", luaopen_coap, true);
    lua_pop(L, 1);
    luaL_requiref(L, "log", luaopen_log, true);
    lua_pop(L, 1);
    luaL_requiref(L, "DaliBus", luaopen_dali, true);
    lua_pop(L, 1);
}


void setfield(lua_State *L, const char *index, int value) {
    lua_pushstring(L, index);
    lua_pushnumber(L, value);
    lua_settable(L, -3);
}


/**
 * Once the initial call to init.lua has been made, everything else ran in LUA is done through this task.
 * 
 */
static void coro_runner(void *aContext)
{
    lua_State *L = luaL_newstate();
    coro_entry_t *coro;
    bool running = true;

    if (!L) {
        ESP_LOGE(TAG, "Could not create state\n");
        abort();
    }
    luaL_openlibs(L);
    load_custom_libs(L);

    int r = luaL_loadfile(L, "/lua/init.lua");
    if (r) {
        lua_report_error(L, r, "Parsing Error");
        running = false;
    }

    // run the main function.
    if (running) {
        r = lua_pcall(L, 0, 0, 0);
        if (r) {
            lua_report_error(L, r, "First Run Error");
        }
    }

    while (running) {
        assert(xQueueReceive(queue, &coro, portMAX_DELAY) == pdTRUE);
        run_coro(L, coro);
    }


    // Close down the LUA Context. 
    lua_close(L);
    ESP_LOGI(TAG, "State closed, heap: %u", xPortGetFreeHeapSize());
    vTaskDelete(NULL);
}




esp_err_t init_lua() {
    esp_err_t err = esp_timer_init();
    assert(err == ESP_OK || err == ESP_ERR_INVALID_STATE); // To allow for somebody else to have already initialised it.

    ESP_ERROR_CHECK(gpio_install_isr_service(0));


    queue = xQueueCreate(10, sizeof(coro_entry_t *));
    if (!queue) {
        ESP_LOGE(TAG, "Could not create event loop run queue");
        return ESP_ERR_NO_MEM;
    }
    xTaskCreate(coro_runner, "lua", 10240, NULL, 5, NULL);
    return ESP_OK;
}