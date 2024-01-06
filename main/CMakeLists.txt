idf_component_register(SRCS 
                    "main.c" 
                    "dali_driver.c" 
                    "lua_system.c"
                    "lua_coap.c"
                    "lua_dali.c"
                    "lua_log.c"
                    "lua_gpio.c"
                    "lua_digest.c"
                    "lua_cbor.c"
                    "dali_rmt_encoder.c" 
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES certs/coap_ca.pem certs/coap_server.crt certs/coap_server.key)


spiffs_create_partition_image(storage ../scripts FLASH_IN_PROJECT)

