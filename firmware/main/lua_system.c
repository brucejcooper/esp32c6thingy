#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_idf_version.h>
#include <esp_system.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <string.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <stdint.h>
#include <esp_mac.h>

#include "lua_system.h"
#include "lua_gpio.h"
#include "lua_log.h"
#include "lua_dali.h"
#include "lua_digest.h"
#include "lua_timer.h"
#include "lua_crypto.h"
#include "lua_openthread.h"
#include "lua_cbor.h"
#include <dirent.h>
#include <sys/stat.h>

#define TAG "lua_system"

// static int await(lua_State *L);
static int restart_system(lua_State *L);
static int get_heap(lua_State *L);
static int lua_uptime(lua_State *L);

typedef struct
{
    lua_callback_helper_t fn;
    void *ctx;
} lua_callback_t;

static const struct luaL_Reg system_funcs[] = {
    // { "start_task", start_task },
    {"restart", restart_system},
    {"heap_info", get_heap},
    {"uptime", lua_uptime},
    {NULL, NULL}};

void lua_report_error(lua_State *L, int status, const char *prefix)
{
    if (status == LUA_OK)
        return;
    ESP_LOGE(TAG, "%s: %s", prefix, lua_tostring(L, -1));
    lua_pop(L, 1);
}

SemaphoreHandle_t mutex;
static lua_State *L;
QueueHandle_t callback_queue;

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

static const code_lookup_t reset_reason_lookup[] = {
    {.sval = "power_on", .ival = RESET_REASON_CHIP_POWER_ON},
    {.sval = "software", .ival = RESET_REASON_CORE_SW},
    {.sval = "deep_sleep", .ival = RESET_REASON_CORE_DEEP_SLEEP},
    {.sval = "mwdt0", .ival = RESET_REASON_CORE_MWDT0},
    {.sval = "rtc_wdt", .ival = RESET_REASON_CORE_RTC_WDT},
    {.sval = "cpu0_mwdt0", .ival = RESET_REASON_CPU0_MWDT0},
    {.sval = "cpu0_software", .ival = RESET_REASON_CPU0_SW},
    {.sval = "cpu0_rtc", .ival = RESET_REASON_CPU0_RTC_WDT},
    {.sval = "brown_out", .ival = RESET_REASON_SYS_BROWN_OUT},
    {.sval = "rtc_wdt", .ival = RESET_REASON_SYS_RTC_WDT},
    {.sval = "super_wdt", .ival = RESET_REASON_SYS_SUPER_WDT},
    // {.sval="clock glitch", .ival=RESET_REASON_SYS_CLK_GLITCH},
    {.sval = "efuse crc", .ival = RESET_REASON_CORE_EFUSE_CRC},
    {.sval = "jtag", .ival = RESET_REASON_CPU0_JTAG},
    {.sval = NULL, .ival = -1}

};

void schedule_callback_from_ISR(lua_callback_helper_t fn, void *ctx)
{
    lua_callback_t cb = {
        .fn = fn,
        .ctx = ctx};
    BaseType_t higherPriorityTaskWoken;

    // If the task queue fills up, panic
    assert(xQueueSendFromISR(callback_queue, &cb, &higherPriorityTaskWoken) == pdTRUE);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

int code_str_to_int(const char *str, const code_lookup_t *lookup)
{
    for (code_lookup_t *l = (code_lookup_t *)lookup; l->sval; l++)
    {
        if (strcmp(l->sval, str) == 0)
        {
            return l->ival;
        }
    }
    return -1;
}

const char *code_int_to_str(int code, const code_lookup_t *lookup)
{
    for (code_lookup_t *l = (code_lookup_t *)lookup; l->sval; l++)
    {
        if (code == l->ival)
        {
            return l->sval;
        }
    }
    return NULL;
}

int lua_lookup(lua_State *L, int argIdx, const code_lookup_t *lookup)
{
    int isnum;
    int ival = lua_tointegerx(L, argIdx, &isnum);
    if (isnum)
    {
        while (lookup->sval != NULL)
        {
            if (lookup->ival == ival)
            {
                return ival;
            }
            lookup++;
        }
        ival = -1;
    }
    else if (lua_isstring(L, argIdx))
    {
        const char *sval = lua_tostring(L, argIdx);
        if (!sval)
        {
            return -1;
        }
        return code_str_to_int(sval, lookup);
    }
    return -1;
}

int check_esp_err(lua_State *L, esp_err_t err)
{
    if (err != ESP_OK)
    {
        luaL_error(L, esp_err_to_name(err));
        return 1;
    }
    return 0;
}

void dumpStack(lua_State *L)
{
    for (int i = lua_gettop(L); i > 0; i--)
    {
        switch (lua_type(L, i))
        {
        case LUA_TBOOLEAN:
            ESP_LOGI(TAG, "[%d] %s", i, lua_toboolean(L, i) ? "true" : "false");
            break;
        case LUA_TSTRING:
            ESP_LOGI(TAG, "[%d] \"%s\"", i, lua_tostring(L, i));
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, i))
            {
                ESP_LOGI(TAG, "[%d] %lld", i, lua_tointeger(L, i));
            }
            else
            {
                ESP_LOGI(TAG, "[%d] %lf", i, lua_tonumber(L, i));
            }
            break;
        default:
            ESP_LOGI(TAG, "[%d] %s %p", i, type_names[lua_type(L, i)], lua_topointer(L, i));
            break;
        }
    }
}

const char *lua_type_str(lua_State *L, int idx)
{
    return type_names[lua_type(L, idx)];
}

lua_State *acquireLuaMutex()
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    return L;
}

void releaseLuaMutex()
{
    if (lua_gettop(L) != 0)
    {
        ESP_LOGW(TAG, "Stack is not-empty after a LUA callback");
        dumpStack(L);
        lua_settop(L, 0);
    }
    xSemaphoreGive(mutex);
}

ccpeed_err_t lua_execute_callback(int fnRef, int nArgs)
{
    assert(lua_gettop(L) == 0);
    assert(lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef));
    if (lua_pcall(L, nArgs, 0, 0))
    {
        ESP_LOGE(TAG, "Error executing function in LUA");
    }
    return CCPEED_NO_ERR;
}

static int restart_system(lua_State *L)
{
    esp_timer_deinit();
    gpio_prepare_for_reset();
    esp_restart();
    return 0;
}

static int get_heap(lua_State *L)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_DEFAULT);

    lua_newtable(L);
    lua_pushstring(L, "free");
    lua_pushinteger(L, info.total_free_bytes);
    lua_settable(L, -3);

    lua_pushstring(L, "min_free");
    lua_pushinteger(L, info.minimum_free_bytes);
    lua_settable(L, -3);

    lua_pushstring(L, "total_allocated");
    lua_pushinteger(L, info.total_allocated_bytes);
    lua_settable(L, -3);
    return 1;
}

bool get_int(lua_State *L, const char *fname, int *out, int default_value)
{
    bool res = false;
    lua_getfield(L, -1, fname);
    if (lua_isinteger(L, -1))
    {
        *out = lua_tointeger(L, -1);
        res = true;
    }
    else if (lua_isnil(L, -1))
    {
        *out = default_value;
        res = true;
    }
    else
    {
        ESP_LOGE(TAG, "Invalid field type");
    }
    lua_pop(L, 1);
    return res;
}

static int io_exists(lua_State *L)
{
    const char *path;
    struct stat st;

    if ((path = luaL_checkstring(L, 1)) != NULL)
    {
        lua_pushboolean(L, stat(path, &st));
    }
    return 1;
}

static int io_readdir(lua_State *L)
{
    const char *dirname = luaL_checkstring(L, 1);
    if (!dirname)
    {
        luaL_argerror(L, 1, "Expected a directory argument");
    }

    DIR *dir = opendir(dirname);
    if (dir == NULL)
    {
        luaL_error(L, "Could not open directory %s", dirname);
    }

    lua_newtable(L);
    int idx = 1;
    struct dirent *de;
    while ((de = readdir(dir)))
    {
        lua_pushinteger(L, idx++);
        lua_pushstring(L, de->d_name);
        lua_settable(L, -3);
    }
    closedir(dir);
    return 1;
}

static int lua_patch_io(lua_State *L)
{
    lua_getglobal(L, "io");

    lua_pushstring(L, "readdir");
    lua_pushcfunction(L, io_readdir);
    lua_settable(L, -3);

    lua_pushstring(L, "exists");
    lua_pushcfunction(L, io_exists);
    lua_settable(L, -3);
    lua_pop(L, 1);
    return 1;
}

static int lua_uptime(lua_State *L)
{
    long long num_secs = esp_timer_get_time() / 1000;
    lua_pushinteger(L, num_secs);
    return 1;
}

int luaopen_system(lua_State *L)
{
    luaL_newlib(L, system_funcs);

    // Set a default_mac string on the system object.
    lua_pushstring(L, "mac_address");
    static uint8_t defaultMac[8];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(defaultMac));
    lua_pushlstring(L, (char *)defaultMac, sizeof(defaultMac));
    lua_settable(L, -3);

    lua_pushstring(L, "reset_reason");
    lua_pushstring(L, code_int_to_str(esp_reset_reason(), reset_reason_lookup));
    lua_settable(L, -3);

    lua_pushstring(L, "firmware");
    const esp_app_desc_t *appdesc = esp_app_get_description();
    lua_newtable(L);
    lua_pushstring(L, "project");
    lua_pushstring(L, appdesc->project_name);
    lua_settable(L, -3);

    lua_pushstring(L, "version");
    lua_pushstring(L, appdesc->version);
    lua_settable(L, -3);

    lua_pushstring(L, "idf_ver");
    lua_pushstring(L, appdesc->idf_ver);
    lua_settable(L, -3);

    lua_pushstring(L, "date");
    lua_pushstring(L, appdesc->date);
    lua_settable(L, -3);

    lua_pushstring(L, "time");
    lua_pushstring(L, appdesc->time);
    lua_settable(L, -3);

    lua_pushstring(L, "sha256");
    lua_pushlstring(L, (const char *)appdesc->app_elf_sha256, sizeof(appdesc->app_elf_sha256));
    lua_settable(L, -3);

    lua_settable(L, -3);

    lua_patch_io(L);
    return 1;
}

static void load_custom_libs(lua_State *L)
{

    luaL_requiref(L, "system", luaopen_system, true);
    lua_pop(L, 1);

    luaL_requiref(L, "gpio", luaopen_gpio, true);
    lua_pop(L, 1);
    luaL_requiref(L, "Logger", luaopen_logger, true);
    lua_pop(L, 1);
    luaL_requiref(L, "DaliBus", luaopen_dali, true);
    lua_pop(L, 1);
    luaL_requiref(L, "digest", luaopen_digest, true);
    lua_pop(L, 1);
    luaL_requiref(L, "cbor", luaopen_cbor, true);
    lua_pop(L, 1);
    luaL_requiref(L, "OpenThread", luaopen_openthread, true);
    lua_pop(L, 1);
    luaL_requiref(L, "Timer", luaopen_timer, true);
    lua_pop(L, 1);
    luaL_requiref(L, "crypto", luaopen_crypto, true);
    lua_pop(L, 1);
}

void run_lua_loop()
{
    esp_err_t err = esp_timer_init();
    assert(err == ESP_OK || err == ESP_ERR_INVALID_STATE); // To allow for somebody else to have already initialised it.

    mutex = xSemaphoreCreateMutex();
    assert(mutex);

    acquireLuaMutex();

    L = luaL_newstate();
    // lua_task_t *coro;
    bool running = true;

    if (!L)
    {
        ESP_LOGE(TAG, "Could not create state\n");
        abort();
    }
    luaL_openlibs(L);
    load_custom_libs(L);

    ESP_LOGI(TAG, "Running '/fs/init.lua' from filesystem");
    int r = luaL_loadfile(L, "/fs/init.lua");
    if (r)
    {
        lua_report_error(L, r, "Parsing Error");
        running = false;
    }

    // run the main function.
    if (running)
    {
        r = lua_pcall(L, 0, 0, 0);
        if (r)
        {
            lua_report_error(L, r, "First Run Error");
        }
    }
    releaseLuaMutex();

    callback_queue = xQueueCreate(10, sizeof(lua_callback_t));
    assert(callback_queue);

    ESP_LOGI(TAG, "Waiting for code to initiate callbacks");
    lua_callback_t cb;
    while (running)
    {
        if (xQueueReceive(callback_queue, &cb, portMAX_DELAY))
        {
            lua_State *L = acquireLuaMutex();
            if (cb.fn)
            {
                cb.fn(L, cb.ctx);
            }
            releaseLuaMutex();
        }
    }

    // Close down the LUA Context.
    lua_close(L);
    ESP_LOGI(TAG, "State closed, heap: %u", xPortGetFreeHeapSize());
}