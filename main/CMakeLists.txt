idf_component_register(SRCS "coap_server_example_main.c" "device.c" "provider.c" "root_provider.c" "dali_provider.c" "dali_rmt_encoder.c" "linked_list.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES certs/coap_ca.pem certs/coap_server.crt certs/coap_server.key)
