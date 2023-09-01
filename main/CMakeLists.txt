idf_component_register(SRCS "coap_server_example_main.c" "connect.c" "dali.c" "dali_rmt_encoder.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES certs/coap_ca.pem certs/coap_server.crt certs/coap_server.key)
