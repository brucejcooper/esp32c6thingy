#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
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
#include "lua_digest.h"
#include "lua_cbor.h"
#include <dirent.h>

#define TAG "lua_system"



typedef struct  {
    int handler_ref;
    int task_ref;
    int arg_ref;
    void *ctx;

    coro_helper_fn_t argumentHelper;
    coro_helper_fn_t postprocessHelper;

    // Fields below are used for event handler state
    int ret;

    esp_timer_handle_t timer;

    // If we're waiting for GPIO, this will be set to the pin that we are waiting on. Will be set to -1 if not waiting on a pin
    int gpio_waiting_pin;
    int gpio_expected_lvl;

    bool dali_cmd_active;
} lua_task_t;


static int await(lua_State *L);
static int start_task(lua_State *L);
static int restart_system(lua_State *L);


static const struct luaL_Reg eventloop_funcs[] = {
    { "await", await },
    { "start_task", start_task },
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



static QueueHandle_t queue;


static void timer_callback(void *arg);



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


typedef enum {
    STATUS_DEAD,
    STATUS_SUSPENDED,
    STATUS_OTHER,
} coro_status_t;




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


const char *lua_type_str(lua_State *L, int idx) {
    return type_names[lua_type(L, idx)];
}


static void schedule_task(lua_task_t *coro) {
    // This can't block, because that would starve the thread that is also draining the queue
    ESP_LOGD(TAG, "CORO %d scheduling", coro->task_ref);
    if (xQueueSend(queue, &coro, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Error enqueueing coro to run");
    }
}





static coro_status_t lua_status_str_to_enum(lua_State *L) {
    const char *val = lua_tostring(L, -1);
    coro_status_t status;
    if (strcmp(val, "dead") == 0) {
        status = STATUS_DEAD;
    } else if (strcmp(val, "suspended") == 0) {
        status =  STATUS_SUSPENDED;
    } else {
        status = STATUS_OTHER;
    }
    return status;
}


static void disable_coro_event_handlers(lua_task_t *coro) {
    esp_timer_stop(coro->timer);
    coro->dali_cmd_active = false;
    if (coro->gpio_waiting_pin > 0) {
        gpio_intr_disable(coro->gpio_waiting_pin);
        coro->gpio_waiting_pin = -1;
    }
}




/**
 * Programattic (C) way of starting a lua task - can be called from any FreeRTOS task.  If you want to start
 * a task from lua directly, look at start_task. 
*/
ccpeed_err_t lua_create_task(int fnRef, void *ctx, coro_helper_fn_t argHelper, coro_helper_fn_t postprocessor) {
    lua_task_t *task = (lua_task_t *) malloc(sizeof(lua_task_t));
    if (!task) {
        return CCPEED_ERROR_NOMEM;
    }
    
    task->handler_ref = fnRef;
    task->task_ref = LUA_NOREF;
    task->argumentHelper = argHelper;
    task->postprocessHelper = postprocessor;
    task->ctx = ctx;
    esp_timer_create_args_t args = {
        .arg = task,
        .callback = timer_callback,
        .dispatch_method = ESP_TIMER_ISR,
    };
    // Create a new one timer to wait for the supplied resperiod. It won't be started yet though.
    if (esp_timer_create(&args, &task->timer) != ESP_OK) {
        return CCPEED_ERROR_NOMEM;
    }

    // Set state values to defaults.
    task->ret = 0;
    task->gpio_waiting_pin = -1;
    task->dali_cmd_active = false;

    schedule_task(task);
    return CCPEED_NO_ERR;
}


static void lua_created_arghandler(lua_State *L, void *arg) {
    // No arguments for lua created tasks.
    lua_pushnil(L);
}

static void lua_created_postprocessor(lua_State *L, void *arg) {
    // Arg 1: boolean - Whether the task succeeded or not
    // Arg 2: the return value (or string error if arg1 is false)

    // We created the refrence when start_task was called.  We no longer need it, so unref it here. 
    int handler_ref = (int) arg;
    luaL_unref(L, LUA_REGISTRYINDEX, handler_ref);


    if (lua_toboolean(L, 1)) {
        ESP_LOGI(TAG, "Coroutine completed with %s result", lua_type_str(L, 2));
    } else {
        ESP_LOGE(TAG, "Coroutine failed: %s", lua_tostring(L, 2));
    }
    lua_pop(L, 2);
}


/**
 * Can be called fron LUA to create a task.  
 * Arg1 - function - the function to run in the task. 
 */
static int start_task(lua_State *L) {
    ESP_LOGD(TAG, "Starting CORO");
    if (!lua_isfunction(L, 1)) {
        luaL_argerror(L, 1, "Expected a function");
        return 1;
    }
    int handlerRef = luaL_ref(L, LUA_REGISTRYINDEX);
    ccpeed_err_t err = lua_create_task(handlerRef, (void *) handlerRef, lua_created_arghandler, lua_created_postprocessor);
    if (err != CCPEED_NO_ERR) {
        luaL_error(L, "Could not create task");
        return 1;
    }
    ESP_LOGD(TAG, "Registered coroutine");
    return 0;
}

static int restart_system(lua_State *L) {
    esp_restart();
    return 0;
}

static int await(lua_State *L) {
    // Expects a single table input, with the appropriate values, but fundamentally it just passes this to yield.
    return lua_yield(L, 1);
}




static void timer_callback(void *arg) {
    lua_task_t *coro = arg;
    BaseType_t higherPriorityTaskWoken;

    ESP_EARLY_LOGD(TAG, "Timer on task %d completed", coro->task_ref);
    disable_coro_event_handlers(coro);
    xQueueSendFromISR(queue, &coro, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken) {
        taskYIELD();
    }
}

static void gpio_intr(void *arg) {
    BaseType_t higherPriorityTaskWoken;
    lua_task_t *coro = arg;
    // Cancel any timer (if running);
    int pin = coro->gpio_waiting_pin;
    disable_coro_event_handlers(coro);


    if (pin > 0) {
        ESP_EARLY_LOGD(TAG, "GPIO interrupt for task %d GPIO %d", coro->task_ref, pin);
        // Disable the interrupt. 
        gpio_isr_handler_remove(pin);
        gpio_set_intr_type(pin, GPIO_INTR_DISABLE);

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
    lua_task_t *coro = arg;
    bool active = coro->dali_cmd_active;
    disable_coro_event_handlers(coro);
    if (active) {
        coro->ret = response;
        xQueueSendFromISR(queue, &coro, &higherPriorityTaskWoken);
        if (higherPriorityTaskWoken) {
            taskYIELD();
        }
    }
}


static void delete_coro(lua_State *L, lua_task_t *coro) {
    disable_coro_event_handlers(coro);
    ESP_ERROR_CHECK(esp_timer_delete(coro->timer));
    if (coro->gpio_waiting_pin != -1) {
        gpio_intr_disable(coro->gpio_waiting_pin);
        gpio_isr_handler_remove(coro->gpio_waiting_pin);
        gpio_set_intr_type(coro->gpio_waiting_pin, GPIO_INTR_DISABLE);
    }
    coro->dali_cmd_active = false;
    luaL_unref(L, LUA_REGISTRYINDEX, coro->task_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, coro->arg_ref);
    free(coro);
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





static void run_coro(lua_State *L, lua_task_t *coro) {
    int delay, pin, level, dali_cmd;

    ESP_LOGD(TAG, "Running Coro %d", coro->handler_ref);

    // Get global "coroutine"
    assert(lua_getglobal(L, "coroutine"));


    if (coro->task_ref == LUA_NOREF) {
        // We haven't created the coroutine yet.  Do it now. 
        assert(lua_getfield(L, -1, "create"));
        assert(lua_rawgeti(L, LUA_REGISTRYINDEX, coro->handler_ref)); // arg
        int r = lua_pcall(L, 1, 1, 0);
        if (r) {
            lua_report_error(L, r, "Could not create coroutine for task");
            return;
        }
        coro->task_ref = luaL_ref(L, LUA_REGISTRYINDEX);

        // Initial call to resume takes an extra argument which is passed to the handler
        assert(lua_getfield(L, -1, "resume"));
        assert(lua_rawgeti(L, LUA_REGISTRYINDEX, coro->task_ref)); // Arg 1

        int top = lua_gettop(L);
        coro->argumentHelper(L, coro->ctx); // arg 2.
        int itemsPushed = lua_gettop(L) - top;
        if (itemsPushed != 1) {
            ESP_LOGE(TAG, "Expected argument helper to return exactly one item");
            if (itemsPushed == 0) {
                lua_pushnil(L);
            } else {
                lua_pop(L, itemsPushed-1);
            }
        }
        // We'll need the argument when calling the post-processing handler, but we also need it for the call below
        // make a copy then take a reference
        lua_pushvalue(L, -1);
        coro->arg_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        // Its a resume from an await callback - We will be returning to the co-routine with the value stored in ret
        assert(lua_getfield(L, -1, "resume"));
        assert(lua_rawgeti(L, LUA_REGISTRYINDEX, coro->task_ref)); // Arg 1
        lua_pushinteger(L, coro->ret); // Arg 2
    }
    

    // Call the resume function.
    int r = lua_pcall(L, 2, 2, 0);
    if (r) {
        lua_report_error(L, r, "Coroutine call failure");
        return;
    }
    // response 1 - boolean - false if an error occurred
    // response 2 - any - a string error or the yeilded/returned value from the coroutine

    // get the state of the coro by calling coroutine.status(coro)
    lua_getfield(L, 1, "status");
    assert(lua_rawgeti(L, LUA_REGISTRYINDEX, coro->task_ref)); // Arg 1
    lua_call(L, 1, 1);
    coro_status_t status = lua_status_str_to_enum(L);
    lua_pop(L, 1); // Get rid of the status string
    lua_remove(L, 1); // Remove the global coroutine object from the bottom of the stack, as we no longer need it.

    ESP_LOGD(TAG, "CORO returned %d %s status %d", lua_toboolean(L, -2), lua_type_str(L, -1), status);
    // Get the response (which should have two responses - whether its finished and any arguments - in the event of an error this will be the second argument)
    bool call_succeded = false;
    if (!lua_isboolean(L, -2)) {
        ESP_LOGE(TAG, "First response should be a boolean");
    }
    call_succeded = lua_toboolean(L, -2);
    bool completed = false;
    if (call_succeded) {
        switch (status) {
            case STATUS_SUSPENDED:
                // The thread yeilded a set of triggers for it to resume upon.
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
                break;


            case STATUS_DEAD:
                // Call completed. send values to post-processor. 
                completed = true;
                break;

            case STATUS_OTHER:
                // This shouldn't happen
                lua_pop(L, 2); // discard whatever it returned, and replace it with an error code. 
                lua_pushboolean(L, false);
                lua_pushstring(L, "Coro in state we don't understand");
                completed = true;
                break;

        }         
    } else {
        // If the second arg is a string, an error occurred.  The value will be an error string. 
        completed = true;
    }

    assert(lua_gettop(L) == 2);
    if (completed) {
        // In addition to the two parameters returned by the coroutine call, we also need to pass the original argument. 
        lua_rawgeti(L, LUA_REGISTRYINDEX, coro->arg_ref);
        // The three parameters, plus the original coroutine global table should be all that is in the stack. 
        coro->postprocessHelper(L, coro->ctx);
        // reset the stack - The postprocess handler might have done something screwy with it.
        lua_settop(L, 0);
        delete_coro(L, coro);
    } else {
        // Clean up the stack.
        // The two parameters, plus the original coroutine global table should be all that is in the stack. 
        lua_pop(L, 2);
    }
}


static int io_readdir(lua_State *L) {
    const char *dirname = luaL_checkstring(L, 1);
    if (!dirname) {
        luaL_argerror(L, 1, "Expected a directory argument");
    }

    DIR* dir = opendir(dirname);
    if (dir == NULL) {
        luaL_error(L, "Could not open directory %s", dirname);
    }

    lua_newtable(L);
    int idx = 1;
    struct dirent *de;
    while ((de = readdir(dir))) {
        lua_pushinteger(L, idx++);
        lua_pushstring(L, de->d_name);
        lua_settable(L, -3);
    }
    closedir(dir);
    return 1;

}


static int lua_patch_io(lua_State *L) {
    lua_getglobal(L, "io");

    lua_pushstring(L, "readdir");
    lua_pushcfunction(L, io_readdir);
    lua_settable(L, -3);

    lua_pop(L, 1);

    return 1;
}


int luaopen_system(lua_State *L) {
    luaL_newlib(L, eventloop_funcs);
    lua_patch_io(L);
    return 1;
}

static void load_custom_libs(lua_State *L) {
    
    luaL_requiref(L, "system", luaopen_system, true);
    lua_pop(L, 1);
    luaL_requiref(L, "gpio", luaopen_gpio, true);
    lua_pop(L, 1);
    luaL_requiref(L, "coap", luaopen_coap, true);
    lua_pop(L, 1);
    luaL_requiref(L, "Logger", luaopen_logger, true);
    lua_pop(L, 1);
    luaL_requiref(L, "DaliBus", luaopen_dali, true);
    lua_pop(L, 1);
    luaL_requiref(L, "digest", luaopen_digest, true);
    lua_pop(L, 1);
    luaL_requiref(L, "cbor", luaopen_cbor, true);
    lua_pop(L, 1);
}


/**
 * Once the initial call to init.lua has been made, everything else ran in LUA is done through this task.
 * 
 */
static void task_runner(void *aContext)
{
    lua_State *L = luaL_newstate();
    lua_task_t *coro;
    bool running = true;

    if (!L) {
        ESP_LOGE(TAG, "Could not create state\n");
        abort();
    }
    luaL_openlibs(L);
    load_custom_libs(L);

    ESP_LOGI(TAG, "Running '/fs/init.lua' from filesystem");
    int r = luaL_loadfile(L, "/fs/init.lua");
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

    queue = xQueueCreate(10, sizeof(lua_task_t *));
    if (!queue) {
        ESP_LOGE(TAG, "Could not create event loop run queue");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Starting LUA task");
    xTaskCreate(task_runner, "lua", 10240, NULL, 5, NULL);
    return ESP_OK;
}