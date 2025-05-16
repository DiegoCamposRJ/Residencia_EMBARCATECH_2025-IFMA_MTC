#include "pico/stdlib.h"
#include "hardware/pwm.h"

#define BUZZER 10

void buzzer_pwm_on(uint freq_hz) {
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER);

    uint32_t clock_freq = 125000000; // Clock do RP2040
    uint32_t wrap = clock_freq / freq_hz - 1;

    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, wrap / 2);
    pwm_set_enabled(slice_num, true);
}

void buzzer_pwm_off() {
    uint slice_num = pwm_gpio_to_slice_num(BUZZER);
    pwm_set_enabled(slice_num, false);
    gpio_set_function(BUZZER, GPIO_FUNC_SIO);
    gpio_init(BUZZER);
    gpio_set_dir(BUZZER, GPIO_OUT);
    gpio_put(BUZZER, 0);
}

void buzzer_beep(uint duration_ms) {
    buzzer_pwm_on(4000);       // Toca 4 kHz
    sleep_ms(duration_ms);
    buzzer_pwm_off();
}

void buzzer_init() {
    gpio_init(BUZZER);
    gpio_set_dir(BUZZER, GPIO_OUT);
    gpio_put(BUZZER, 0);
}
