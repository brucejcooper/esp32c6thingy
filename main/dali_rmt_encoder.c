#include "esp_check.h"
#include "dali_rmt_encoder.h"

static const char *TAG = "dali_encoder";

typedef struct {
    rmt_encoder_t base;           // the base "class", declares the standard encoder interface
    rmt_encoder_t *copy_encoder;  // use the copy_encoder to encode the leading and ending pulse
    rmt_encoder_t *bytes_encoder; // use the bytes_encoder to encode the address and command data
    rmt_symbol_word_t start_bit;    // how a start bit is represented
    int state;
} rmt_dali_encoder_t;

static size_t rmt_encode_dali(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_dali_encoder_t *dali_encoder = __containerof(encoder, rmt_dali_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    rmt_encoder_handle_t copy_encoder = dali_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = dali_encoder->bytes_encoder;
    switch (dali_encoder->state) {
    case 0: // send Start bit
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &dali_encoder->start_bit,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            dali_encoder->state = 1; // we can only switch to next state when current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    // fall-through
    case 1: // send address
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            // dali_encoder->state = 2; // we can only switch to next state when current encoder finished
            dali_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;

        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_dali_encoder(rmt_encoder_t *encoder)
{
    rmt_dali_encoder_t *dali_encoder = __containerof(encoder, rmt_dali_encoder_t, base);
    rmt_del_encoder(dali_encoder->copy_encoder);
    rmt_del_encoder(dali_encoder->bytes_encoder);
    free(dali_encoder);
    return ESP_OK;
}

static esp_err_t rmt_dali_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_dali_encoder_t *dali_encoder = __containerof(encoder, rmt_dali_encoder_t, base);
    rmt_encoder_reset(dali_encoder->copy_encoder);
    rmt_encoder_reset(dali_encoder->bytes_encoder);
    dali_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t rmt_new_dali_encoder(rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_dali_encoder_t *dali_encoder = NULL;
    dali_encoder = calloc(1, sizeof(rmt_dali_encoder_t));
    ESP_GOTO_ON_FALSE(dali_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for DALI encoder");
    dali_encoder->base.encode = rmt_encode_dali;
    dali_encoder->base.del = rmt_del_dali_encoder;
    dali_encoder->base.reset = rmt_dali_encoder_reset;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &dali_encoder->copy_encoder), err, TAG, "create copy encoder failed");

    // construct the leading code and ending code with RMT symbol format
    dali_encoder->start_bit = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = 416ULL,
        .level1 = 0,
        .duration1 = 416ULL,
    };

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 0,
            .duration0 = 416ULL,
            .level1 = 1,
            .duration1 = 416ULL,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 416ULL,
            .level1 = 0,
            .duration1 = 416ULL,
        },
        .flags.msb_first = 1,
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &dali_encoder->bytes_encoder), err, TAG, "create bytes encoder failed");

    *ret_encoder = &dali_encoder->base;
    return ESP_OK;
err:
    if (dali_encoder) {
        if (dali_encoder->bytes_encoder) {
            rmt_del_encoder(dali_encoder->bytes_encoder);
        }
        if (dali_encoder->copy_encoder) {
            rmt_del_encoder(dali_encoder->copy_encoder);
        }
        free(dali_encoder);
    }
    return ret;
}