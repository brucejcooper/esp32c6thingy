
idf_component_register(SRCS 
                    "main.c" 
                    "dali_driver.c" 
                    "lua_system.c"
                    "lua_dali.c"
                    "lua_log.c"
                    "lua_gpio.c"
                    "lua_digest.c"
                    "lua_openthread.c"
                    "lua_timer.c"
                    "lua_cbor.c"
                    "dali_rmt_encoder.c" 
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES certs/coap_ca.pem certs/coap_server.crt certs/coap_server.key)

target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE")
