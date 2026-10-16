// Synthetic benchmark input for FRED evaluation task 07. MIT licensed, original.
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include "meter_extra.h"

int meter_legacy_format(char* out, size_t size, float volts);

typedef struct {
    FuriHalAdcHandle* adc;
    FuriTimer* timer;
    float volts;
} Meter;

static void meter_tick(void* context) {
    Meter* meter = context;
    uint16_t raw = furi_hal_adc_read(meter->adc, FuriHalAdcChannel1);
    meter->volts = furi_hal_adc_convert_to_voltage(meter->adc, raw);
}

int32_t meter_app(void* p) {
    UNUSED(p);
    Meter meter = {0};
    char text[16];
    meter.adc = furi_hal_adc_acquire();
    furi_hal_adc_configure(meter.adc);
    cfw_meter_set_backlight_boost(2);
    meter.timer = furi_timer_alloc(meter_tick, FuriTimerTypePeriodic, &meter);
    furi_timer_start(meter.timer, 100);
    furi_delay_ms(1000);
    meter_legacy_format(text, sizeof(text), meter.volts);
    furi_timer_free(meter.timer);
    furi_hal_adc_release(meter.adc);
    return 0;
}
