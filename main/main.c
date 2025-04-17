/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <stdio.h>



#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

const int BOTAO_PULO = 2;
const int BOTAO_CORRER = 3;
const int BOTAO_START = 4;
const int BOTAO_POWER = 5;

// Declar filas
QueueHandle_t xQueueAdcx;
QueueHandle_t xQueueAdcy;
QueueHandle_t xQueueAdc2x;
QueueHandle_t xQueueAdc2y;

QueueHandle_t xQueueProcessx;
QueueHandle_t xQueueProcessy;

QueueHandle_t xQueueBotao;


typedef struct {
    int axis; //X (0) ou Y (1)
    int val;    //   o valor lido do joystick
} adc_t;



// Aplica media movel eixo Y
void process_taskY(void *p) {

    adc_t struct1;
    int indice=0;
    int buffer[5]={0, 0, 0, 0, 0};
    int sum;

    while (true) {
        if ( xQueueReceive( xQueueAdcy, &struct1, 100) ) {
            buffer[indice]=struct1.val;
            sum=0;
            for ( int i=0; i<5; i++ ) {
                sum+=buffer[i];
            }
            int med_movel=sum/5;
            indice=( indice+1 )%5;
            adc_t processed_data={ struct1.axis , med_movel };
            if(&processed_data > 0){
                xQueueSend( xQueueProcessy, &processed_data, 500 );
            }
                
            vTaskDelay( pdMS_TO_TICKS(40) );
        }
    }
}

    // Aplica media movel eixo X
void process_taskX(void *p) {
    adc_t struct1;
    // Buffer que será usado para média móvel
    int indice=0;
    int sum;
    int buffer[5]={0, 0, 0, 0, 0};

    while (true) {
        if ( xQueueReceive(xQueueAdcx, &struct1, 100) ) {
            buffer[indice]=struct1.val;
            sum=0;
            for ( int i=0; i<5; i++ ) {
                sum+=buffer[i];
            }
            int med_movel=sum/5;
            indice=( indice+1 )%5;
            adc_t processed_data={ struct1.axis, med_movel };
            if(&processed_data > 0){
                xQueueSend( xQueueProcessx, &processed_data, 500 );
            }

            vTaskDelay( pdMS_TO_TICKS(40) );
        }
    }
}


// Tarefa que lê o ADC do eixo Y, aplica média e envia para fila final
void y_task(void *params) {

    while (true) {
        adc_select_input(1);
        uint16_t resultado=adc_read();
        adc_t struct1={1, resultado};
        xQueueSend( xQueueAdcy, &struct1, 500 );
        adc_t resultado_processado;
        if ( xQueueReceive(xQueueProcessy, &resultado_processado, 100) ) {

            xQueueSend( xQueueAdc2y, &resultado_processado, 100 );
        }
        vTaskDelay( pdMS_TO_TICKS(150) );
    }
}

// Tarefa que lê o ADC do eixo X, aplica média e envia para fila final
void x_task(void *params) {

    while (1) {
        adc_select_input(0);
        uint16_t resultado=adc_read();
        adc_t struct1={ 0, resultado };
        xQueueSend( xQueueAdcx, &struct1, 500 );
        adc_t resultado_processado;
        if ( xQueueReceive(xQueueProcessx, &resultado_processado, 100) ) {
            xQueueSend( xQueueAdc2x, &resultado_processado, 100 );
        }
        vTaskDelay( pdMS_TO_TICKS(150) );
    }
}

// Funcao que escala o valor do ADC para o intervalo do mouse -255 a +255
int escala_mouse(int adc_val) {
    int max_mouse=255;
    int val_central=2047;
    int filtro=adc_val-val_central;
    int filtro2=filtro/(val_central/max_mouse);
    // ZONA MORTA:
    if ( filtro2>-80 && filtro2<80 ) {
        filtro2=0;
    }
    return filtro2;
}

// Tarefa que envia os dados convertidos para o programa Python via UART
void uart_task(void *params) {
    adc_t Y;
    adc_t X;
    adc_t botao;

    while (true) {
        // Se houver dado novo do eixo X, envia pela UART no formato esperado
        if ( xQueueReceive(xQueueAdc2x, &X, portMAX_DELAY) ) {
            int x_na_escala=escala_mouse(X.val);
            // Garante que só vai pra frente o que for diferente de 0, assim, nao acumula
            if( escala_mouse(X.val) != 0 ){
                uart_putc_raw(UART_ID, 0);
                uart_putc_raw(UART_ID, x_na_escala & 0xFF);
                uart_putc_raw(UART_ID, (x_na_escala >> 8) & 0xFF);
                uart_putc_raw(UART_ID, 0xFF);
                // printf("%d", X.val);
            }
        }

        // Se houver dado novo do eixo Y, envia pela UART no formato esperado
        if ( xQueueReceive( xQueueAdc2y, &Y , portMAX_DELAY) ) {
            int y_na_escala = escala_mouse(Y.val);
            // Garante que só vai pra frente o que for diferente de 0, assim, nao acumula
            if( escala_mouse(Y.val)!=0 ){
                uart_putc_raw(UART_ID, 1);
                uart_putc_raw(UART_ID, y_na_escala & 0xFF);
                uart_putc_raw(UART_ID, (y_na_escala >> 8) & 0xFF);
                uart_putc_raw(UART_ID, 0xFF);
            }            
        }

        if (xQueueReceive(xQueueBotao, &botao, 0)) {
            uart_putc_raw(UART_ID, botao.axis);  // ID do botão (ex: 2 = pulo)
            uart_putc_raw(UART_ID, botao.val);   // 1 = pressionado
            uart_putc_raw(UART_ID, 0);           // reservado
            uart_putc_raw(UART_ID, 0xFF);        // delimitador
        }

    }
}

void botao_callback(uint gpio, uint32_t events) {
    static bool ligado = true; // Estado inicial
    adc_t struct1;

    if (events & GPIO_IRQ_EDGE_FALL) {
        if (!ligado && gpio != BOTAO_POWER) return;  // Ignora tudo se desligado

        switch (gpio) {
            case BOTAO_PULO:
                struct1.axis = 2;
                struct1.val = 1;
                break;
        
            case BOTAO_CORRER:
                struct1.axis = 3;
                struct1.val = 1;
                break;
        
            case BOTAO_START:
                struct1.axis = 4;
                struct1.val = 1;
                break;
        
            case BOTAO_POWER:
                ligado = !ligado;
                struct1.axis = 5;
                struct1.val = ligado ? 1 : 0;
                break;
        
            default:
                return; // Ignora botões desconhecidos
        }

        xQueueSendFromISR(xQueueBotao, &struct1, NULL);

    }
}

void init_botoes() {
    uint botoes[] = {BOTAO_PULO, BOTAO_CORRER, BOTAO_START, BOTAO_POWER};

    for (int i = 0; i < 4; i++) {
        gpio_init(botoes[i]);
        gpio_set_dir(botoes[i], GPIO_IN);
        gpio_pull_up(botoes[i]);
        gpio_set_irq_enabled_with_callback(botoes[i], GPIO_IRQ_EDGE_FALL, true, botao_callback);
    }

    gpio_init(BOTAO_POWER);
    gpio_set_dir(BOTAO_POWER, GPIO_IN);
    gpio_pull_up(BOTAO_POWER);
    gpio_set_irq_enabled_with_callback(BOTAO_POWER, GPIO_IRQ_EDGE_FALL, true, botao_callback);
}


int main() {

    //Inicializando entrada, saida, UART e ADC
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);
    adc_init();

    //Definindo a funcao dos pinos  
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    init_botoes();

    adc_gpio_init(26); // eixo X
    adc_gpio_init(27); // eixo Y

    // Crisa as filas para comunicação entre tarefas
    xQueueProcessx = xQueueCreate(64, sizeof(adc_t));
    xQueueProcessy = xQueueCreate(64, sizeof(adc_t));

    xQueueAdcx = xQueueCreate(64, sizeof(adc_t));
    xQueueAdcy = xQueueCreate(64, sizeof(adc_t));

    xQueueAdc2x = xQueueCreate(64, sizeof(adc_t));
    xQueueAdc2y = xQueueCreate(64, sizeof(adc_t));

    xQueueBotao = xQueueCreate(64, sizeof(adc_t));

// Criando as tarefas do sistema
    xTaskCreate(x_task, "TASK_X", 512, NULL, 1, NULL);
    xTaskCreate(y_task, "TASK_Y", 512, NULL, 1, NULL);

    xTaskCreate(uart_task, "UART", 512, NULL, 1, NULL);

    xTaskCreate(process_taskX, "Process_X", 512, NULL, 1, NULL);
    xTaskCreate(process_taskY, "Process_Y", 512, NULL, 1, NULL);
 


    vTaskStartScheduler();
    while (true);
}
