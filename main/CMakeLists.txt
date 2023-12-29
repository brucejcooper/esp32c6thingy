idf_component_register(SRCS 
                    "main.c" 
                    "device.c" 
                    "provider.c" 
                    "root_provider.c" 
                    "dali_provider.c" 
                    "lua_event_loop.c"
                    "lua_device.c"
                    "lua_gpio.c"
                    "dali_rmt_encoder.c" 
                    "linked_list.c"
                    "cbor_helpers.c"
                    "dali_device.c"
                    "gpio_input_provider.c"
                    "subscriptions.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES certs/coap_ca.pem certs/coap_server.crt certs/coap_server.key)


spiffs_create_partition_image(storage ../scripts FLASH_IN_PROJECT)

