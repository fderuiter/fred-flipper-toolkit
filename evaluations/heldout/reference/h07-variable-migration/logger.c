// Reviewer-side migrated H07 logger. MIT licensed, original.
// Not buildable: the SDK tables are synthetic. Shows the expected source changes.
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <toolbox/units.h> /* legacy_units.h removed; same exported function */
#include <toolbox/sample_rates.h>

#define TAG "Logger"

typedef struct {
    uint32_t rate_hz; /* table element widened to uint32_t in 88.4 */
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
        "%lu s @ %lu Hz: %s",
        (unsigned long)(furi_hal_rtc_get_timestamp() - logger->started),
        (unsigned long)logger->rate_hz,
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
