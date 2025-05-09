/*
RESIDÊNCIA PROFISSIONAL EM SISTEMAS EMBARCADOS-IFMA
======Projeto: Monitoramento de Som com Interrupção de Timer======
Desenvolvido por Diego da S.C do Nascimento
Matrícula:20251RSE.MTC0017

*/
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
#define MAX_AMPLITUDE 3000 // Ajuste conforme necessidade 2000 -3000
#define SOUND_THRESHOLD 800 //limiar de detecção de som
#define GAIN 3.3f //fator de ganho (3.3 max) 

// Variáveis globais
volatile uint16_t mic_raw = 0;
volatile int16_t mic_amplitude = 0;
float filtered_amplitude = 0;
#define FILTER_ALPHA 0.7f  // Fator de suavizar sinal do microfone, reduzindo ruído

PIO pio = pio0; //Seleciona o bloco PIO
uint sm; //Guarda o número da máquina
uint32_t led_colors[NUM_LEDS]; //Buffer que armazena as cores

// Buffer para armazenar as cores dos LEDs (24 bits por LED)
uint32_t led_colors[NUM_LEDS];

// Inicializa o PIO e state machine para WS2812
void neoPixel_init() {
    uint offset = pio_add_program(pio, &ws2812_program);
    if (offset == (uint)-1) {
        printf("Erro: não foi possível adicionar o programa PIO\n");
        return;
    }
    sm = pio_claim_unused_sm(pio, true);
    if (sm == (uint)-1) {
        printf("Erro: não foi possível alocar state machine\n");
        return;
    }
    ws2812_program_init(pio, sm, offset, NEOPIXEL_PIN, 800000, false);
    printf("PIO inicializado: offset=%u, sm=%u\n", offset, sm);
}

// Atualiza o buffer do LED na posição index com cor RGB
void neoPixel_set_pixel_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    // WS2812 usa ordem GRB
    led_colors[index] = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
}

// Envia o buffer completo para a fita WS2812
void neoPixel_show() {
    for (int i = 0; i < NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, led_colors[i] << 8u);
    }
    // Pequena pausa para garantir atualização (reset)
    sleep_us(80);
}

// Atualiza o buffer com gráfico de barras colorido e envia para a matrix
void update_leds_bar(int amplitude) {
    const int low_limit = MAX_AMPLITUDE / 3;
    const int mid_limit = 2 * MAX_AMPLITUDE / 3;

    int leds_to_light = (amplitude * NUM_LEDS) / MAX_AMPLITUDE;
    if (leds_to_light > NUM_LEDS) leds_to_light = NUM_LEDS;

    for (int i = 0; i < NUM_LEDS; i++) {
        if (i < leds_to_light) {
            if (amplitude < low_limit) {
                // Verde suave (baixa intensidade)
                uint8_t intensity = 10 + (i * 5);
                neoPixel_set_pixel_color(i, 0, intensity, 0);
            } else if (amplitude < mid_limit) {
                // Amarelo (médio: vermelho + verde)
                uint8_t intensity = 15 + (i * 8);
                neoPixel_set_pixel_color(i, intensity, intensity, 0);
            } else {
                // Vermelho forte (alta intensidade)
                uint8_t intensity = 30 + (i * 15);
                neoPixel_set_pixel_color(i, intensity, intensity / 4, 0);
            }
        } else {
            // LEDs apagados
            neoPixel_set_pixel_color(i, 0, 0, 0);
        }
    }
    neoPixel_show();
}
//test
void test_leds() {
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i == 0) {
            neoPixel_set_pixel_color(i, 255, 0, 0); // Vermelho forte
        } else {
            neoPixel_set_pixel_color(i, 0, 0, 0);   // Apaga
        }
    }
    neoPixel_show();
}


// Timer callback para leitura do microfone
bool timer_callback(repeating_timer_t *rt) {
    mic_raw = adc_read();
    mic_amplitude = mic_raw - 2048;
    // Amplifica o sinal para compensar suavização
    //float amplified = mic_amplitude * 2.0f;
    
    filtered_amplitude = FILTER_ALPHA * mic_amplitude + (1 - FILTER_ALPHA) * filtered_amplitude;
    filtered_amplitude *= GAIN;  // Amplifica o sinal filtrado

    // Limita para evitar overflow
    if (filtered_amplitude > 32767) filtered_amplitude = 32767;
    if (filtered_amplitude < -32768) filtered_amplitude = -32768;
    return true;
}

void microphone_init() {
    adc_init();
    adc_gpio_init(MIC_PIN);
    adc_select_input(2);
}

void debug_microphone() {
    printf("Filtrado: %.2f\n", filtered_amplitude);
}

int main() {
    stdio_init_all();
    microphone_init();
    neoPixel_init();

    repeating_timer_t timer;
    add_repeating_timer_ms(-10, timer_callback, NULL, &timer);

    while (1) {
        int amplitude = abs((int)filtered_amplitude);
        // Atualiza a matrix com o gráfico colorido
        if (amplitude > SOUND_THRESHOLD) {
            update_leds_bar(amplitude);  // Som detectado: atualiza LEDs
        } else {
            update_leds_bar(0);          // Som abaixo do limiar: apaga LEDs
        }
        
        debug_microphone();
        sleep_ms(50);
    }
}

