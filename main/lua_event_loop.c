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

#include "lua_event_loop.h"
#include "lua_gpio.h"
#include "lua_device.h"



#define TAG "lua_event_loop"

static int ldelay(lua_State *L) {
    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
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
} coro_entry_t;


#define MAX_COROS 10
static int num_coros = 0;
static coro_entry_t coros[MAX_COROS];

static QueueHandle_t queue;


static void timer_callback(void *arg);


static void schedule_coro(coro_entry_t *coro) {
    // This can't block, because that would starve the thread that is also draining the queue
    if (xQueueSend(queue, &coro, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Error enqueueing coro to run");
    }
}


static int start_coro(lua_State *L) {
    if (!lua_isthread(L, 1)) {
        luaL_argerror(L, 1, "Expected a coroutine");
        return 0;
    }
    // Store the coroutine as a reference in the registry.
    if (num_coros >= MAX_COROS) {
        luaL_error(L, "Too many co-routines");
        return 0;
    }
    coro_entry_t *coro = &coros[num_coros++];
    coro->task_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    coro->ret = 0;
    coro->gpio_waiting_pin = -1;

    // Each coro gets its own high res timer, whether it uses it or not. 
    esp_timer_create_args_t args = {
        .arg = coro,
        .callback = timer_callback,
        .dispatch_method = ESP_TIMER_ISR,
    };
    // Create a new one shot timer to wait for the supplied period. 
    ESP_ERROR_CHECK(esp_timer_create(&args, &coro->timer));
    schedule_coro(coro);

    ESP_LOGI(TAG, "Registered coroutine with id %d", coro->task_ref);
    return 0;
}

static const struct luaL_Reg eventloop_funcs[] = {
    { "delay", ldelay },
    { "start_coro", start_coro },
    { NULL, NULL }
};


static void report(lua_State *L, int status, const char *prefix) {
    if (status == LUA_OK)
        return;

    const char *msg = lua_tostring(L, -1);
    ESP_LOGE(TAG, "%s: %s\n", prefix, msg);
    lua_pop(L, 1);
}


static void timer_callback(void *arg) {
    coro_entry_t *coro = arg;
    BaseType_t higherPriorityTaskWoken;

    ESP_EARLY_LOGD(TAG, "Timer on task %d completed", coro->task_ref);
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


static void delete_coro(lua_State *L, coro_entry_t *coro) {
    int i = coros - coro;

    ESP_ERROR_CHECK(esp_timer_delete(coro->timer));
    if (coro->gpio_waiting_pin) {
        gpio_intr_disable(coro->gpio_waiting_pin);
        gpio_isr_handler_remove(coro->gpio_waiting_pin);
        gpio_set_intr_type(coro->gpio_waiting_pin, GPIO_INTR_DISABLE);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, coro->task_ref);
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

static void run_coro(lua_State *L, coro_entry_t *coro) {
    int blockType, delay, pin, level;

    ESP_LOGD(TAG, "Running Coro %d", coro->task_ref);

    // Get global "coroutine"
    assert(lua_getglobal(L, "coroutine"));
    // Find field (function) "resume"
    assert(lua_getfield(L, -1, "resume"));
    // Push the coroutine argument 
    assert(lua_rawgeti(L, LUA_REGISTRYINDEX, coro->task_ref));

    lua_pushinteger(L, coro->ret);

    // Run the method with two arguments (the coro and the ret value from the last event), expecting two responses (although the second one might be nil)
    int r = lua_pcall(L, 2, 2, 0);
    if (r) {
        report(L, r, "Coroutine call failure");
    }
    // Get the response (which should have two responses - whether its finished and any arguments - in the event of an error this will be the second argument)
    bool still_running = false;
    if (!lua_isboolean(L, -2)) {
        ESP_LOGE(TAG, "First response should be a boolean");
    }
    still_running = lua_toboolean(L, -2);
    if (still_running) {
        bool wait_condition_processed = false;
        if (lua_istable(L, -1)) {
            // An await may include a timeout
            if (get_int(L, "timeout", &delay, -1)) {
                if (delay > 0) {
                    ESP_LOGD(TAG, "Timeout in %d ms", delay);
                    ESP_ERROR_CHECK(esp_timer_start_once(coro->timer, delay*1000));
                }
                wait_condition_processed = true;
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
                    wait_condition_processed = true;
                }
            } else {
                ESP_LOGE(TAG, "Invalid delay");
            }


        } else if (lua_isnil(L, -1)) {
            // Just reschedule immeditately.  This allows for a long running task simply to yield occasionally to be nice.
            wait_condition_processed = true;
        } else {
            ESP_LOGW(TAG, "Incorrect response type from yield.  Will just reschedule immediately");
        }

        if (!wait_condition_processed) {
            ESP_LOGW(TAG, "Wait condition not properly processed.  Re-scheduling task. ");
            schedule_coro(coro);
        }
    } else {
        // If the second arg is a string, then an error has occurred. 
        if (lua_isstring(L, -1)) {
            const char *msg = lua_tostring(L, -1);
            ESP_LOGE(TAG, "Error running co-routine %d: %s", coro->task_ref, msg);
        }    
    }

    if (!still_running) {
        ESP_LOGI(TAG, "Coro %d is now complete", coro->task_ref);

        delete_coro(L, coro);
    }
    // Update the coro status.

    // Dispose of the responses.
    lua_pop(L, 2);
}





int luaopen_eventloop(lua_State *L) {
    luaL_newlib(L, eventloop_funcs);
    return 1;
}

static void load_custom_libs(lua_State *L) {
    
    luaL_requiref(L, "event_loop", luaopen_eventloop, true);
    lua_pop(L, 1);
    luaL_requiref(L, "device", luaopen_device, true);
    lua_pop(L, 1);
    luaL_requiref(L, "gpio", luaopen_gpio, true);
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
        report(L, r, "Parsing Error");
        running = false;
    }

    // run the main function.
    if (running) {
        r = lua_pcall(L, 0, 0, 0);
        if (r) {
            report(L, r, "First Run Error");
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