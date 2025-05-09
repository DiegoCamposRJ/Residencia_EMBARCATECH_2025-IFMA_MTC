#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

#define MIC_PIN 28        // GPIO28 (ADC2)
#define NEOPIXEL_PIN 7    // GPIO7
#define SAMPLE_RATE 10000 // 10ms
#define NUM_LEDS 8        // Número de LEDs na matriz
#define MAX_AMPLITUDE 200 // Ajuste conforme seu microfone e filtro

// Variáveis globais para compartilhamento entre interrupção e main
volatile uint16_t mic_raw = 0;
volatile int16_t mic_amplitude = 0;
uint32_t neopixel_buffer[NUM_LEDS] = {0};

PIO pio = pio0;
uint sm;
// Configuração do PIO para NeoPixel
void neoPixel_init() {
    uint offset = pio_add_program(pio, &ws2812_program);
    sm = pio_claim_unused_sm(pio, true);
    ws2812_program_init(pio, sm, offset, NEOPIXEL_PIN, 800000, false);
}
void neoPixel_set_pixel(uint index, uint8_t r, uint8_t g, uint8_t b) {
    (void)index; // índice não usado neste exemplo simples
    uint32_t color = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    pio_sm_put_blocking(pio, sm, color << 8u);
}

// Função para atualizar LEDs
/**void update_leds(int amplitude) {
    uint8_t intensity = (amplitude >> 6); // Ajuste de ganho
    for(int i=0; i<NUM_LEDS; i++){
        neopixel_buffer[i] = (intensity << 8); // Verde com intensidade variável
    }
    for(int i=0; i<NUM_LEDS; i++){
        pio_sm_put_blocking(pio0, 0, neopixel_buffer[i] << 8u);
    }
}**/
//Função em Barra
void update_leds_bar(int num_leds) {
    for(int i = 0; i < NUM_LEDS; i++) {
        if(i < num_leds) {
            // LED aceso (exemplo: verde)
            neoPixel_set_pixel(i, 0, 50, 0);
        } else {
            // LED apagado
            neoPixel_set_pixel(i, 0, 0, 0);
        }
    }
}


// Timer callback para leitura do microfone
bool timer_callback(repeating_timer_t *rt) {
    mic_raw = adc_read();              // Leitura bruta do ADC [2][5]
    mic_amplitude = mic_raw - 2048;    // Remove bias DC [2]
    return true;
}

// Função de inicialização do microfone
void microphone_init() {
    adc_init();
    adc_gpio_init(MIC_PIN);
    adc_select_input(2); // Canal ADC2 (GPIO28) [5]
}

// Função para debug do microfone
void debug_microphone() {
    printf("Raw: %d, Amplitude: %d\n", mic_raw, mic_amplitude);
}

int main() {
    stdio_init_all();
    microphone_init();
    neoPixel_init();

    // Configura timer periódico [6]
    repeating_timer_t timer;
    add_repeating_timer_ms(-10, timer_callback, NULL, &timer);

    while(1) {
        int amplitude = abs(mic_amplitude);

        // Normaliza para o número de LEDs
        int leds_to_light = (amplitude * NUM_LEDS) / MAX_AMPLITUDE;
        if(leds_to_light > NUM_LEDS) leds_to_light = NUM_LEDS;
        update_leds_bar(leds_to_light);
        //update_leds(abs(mic_amplitude)); // Usa valor absoluto para intensidade
        
       /*for (int i = 0; i < NUM_LEDS; i++) {
        neoPixel_set_pixel(i, 0, 50, 0); // verde
        }
        sleep_ms(500);
        for (int i = 0; i < NUM_LEDS; i++) {
        neoPixel_set_pixel(i, 0, 0, 0); // apaga
        }*/
        debug_microphone();
        sleep_ms(100);
    }
}
