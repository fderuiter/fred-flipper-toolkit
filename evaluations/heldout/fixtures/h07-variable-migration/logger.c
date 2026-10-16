// Synthetic benchmark input for a FRED held-out task. MIT licensed, original.
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <toolbox/legacy_units.h>
#include <toolbox/sample_rates.h>

#define TAG "Logger"

typedef struct {
    uint16_t rate_hz;
    uint32_t started;
    char last[24];
} Logger;

static void logger_pick_rate(Logger* logger, uint8_t index) {
    if(index >= logger_rate_count) index = 0;
    logger->rate_hz = logger_rate_table[index];
}

static void logger_record(Logger* logger, float celsius) {
    units_format_celsius(logger->last, sizeof(logger->last), celsius);
    FURI_LOG_I(
        TAG,
        "%lu s @ %u Hz: %s",
        (unsigned long)(furi_hal_rtc_get_timestamp() - logger->started),
        logger->rate_hz,
        logger->last);
}

int32_t logger_app(void* p) {
    UNUSED(p);
    Logger logger = {0};
    logger.started = furi_hal_rtc_get_timestamp();
    logger_pick_rate(&logger, 3);
    for(int i = 0; i < 5; i++) {
        logger_record(&logger, 21.5f + (float)i * 0.1f);
        furi_delay_ms(1000u / (logger.rate_hz ? logger.rate_hz : 1u) + 1u);
    }
    return 0;
}
