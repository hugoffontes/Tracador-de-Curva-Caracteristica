/*
 * File:   tracer_alpha.c
 * Author: hugoff
 *
 * Created on January 19, 2022, 13:34 PM
 */

// FCY = (source_oscillator_freq*PLL)/(postscaler*4) = Fosc/4
// PLL = 1, Postscaler = 1 => FCY = source_oscillator/4 = 6000000/4 = 1500000
#define FCY 1500000

#include <xc.h>
#include <libpic30.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "configuration.h"

// Constantes para conversão A/D e D/A
#define const_v_max 10.0
#define const_v_min -10.0
#define const_v_niveis 0x7f // 8 bits de conversão D/A
#define const_bin_v_min 0xff // valor binário da tensão de saída mais negativa para circuito em escada R-2R com amplificador inversor
// #define const_bin_v_max 0x00
#define const_i_max 40.0 // 40 mA
#define const_i_min -40.0 // 40 mA
#define const_i_niveis 0x3ff // 10 bits de conversão A/D


// pino RB8 como sinal analógico
#define ADPCFG = 0xFEFF


// Variáveis para comunicação serial
unsigned int baud_rate_desired = 9600;
char input_char;
//char* input_str;
char input_str[50];
//char output_str[50];
char* output_str;
char curva_str[3];
char vbe_str[8];
char* vce_str; // sinal.dezena.unidade.décimo.centésimo.separador.
char* ice_str;
__attribute__((far)) char vce_vec_str[8]; // vce_str*níveis + nova linha.null  = 6*256 + 2 = 1538
__attribute__((far)) char ice_vec_str[8];

// Variáveis para conversão A/D e D/A
uint16_t adc_out = 0;
float vce = 0.0;
float vce_vec[const_v_niveis];
float vbe = 0.0;
float vbe_min = 0.0;
float vbe_max = 0.0;
float vbe_step = 0.0;
uint8_t vce_bin = 0;
uint8_t vbe_bin = 0;
float ice_max = const_i_max;
float ice_min = const_i_min;

float v_max = const_v_max;
float v_min = const_v_min;
int bin_v_min = const_bin_v_min ;
//int bin_v_max = const_bin_v_max;
float v_step;// = (const_v_max - const_v_min)/const_bin_v_min;

int delay_sh_ms = 100;
int delay_adc_ms = 10;

int varredura_completa = 0;
float amostra_ad_out;
int terminais = 2;
int curva = 1;
int ftoi_casas = 100;

void floattoint_str(float f, char* int_str){
  int inteiro = f*ftoi_casas;
  sprintf(int_str,"%i",inteiro);
}

void blink(){
  _LATD1 = 0;
  __delay_ms(100);
  _LATD1 = 1;
  __delay_ms(100);
  _LATD1 = 0;
}

int DAC_setup(void){
    v_step = (v_max - v_min)/bin_v_min;
    int i = 0;
    for (i = 0 ; i <= const_v_niveis; i++){
      vce_vec[i] = v_min + (i*v_step);
    }
    // configuração de ports
    // RD e RF como saída digital
    // RF0-6,RD0: saída digital para controle do conversor d/a
    // RD2: controle do amostrador/retentor (0 => amostrando, 1 => retendo)
    TRISD = 0;
    TRISF = 0;
    LATD = 0;
    LATF = 0;

    return 0;
}

int ADC_setup(void){
    // pino RB8 como entrada
    _TRISB8 = 1;

    // configurando conversão A/D
    ADCON1bits.ADON = 0; // desabilitando conversão
    ADCON1bits.ASAM = 1; // habilita autosample
    ADCON1bits.FORM = 0b00; // saída em formato de número inteiro sem sinal

    ADCON2 = 0; // tens?es de refer?ncia AVDD e AVSS, buffer de 16 bits

    //TAD_min >= 154 ns
    //TCY = 1/FCY = 500 ns
    //ADCS<5:0> = 2*(TAD_min/TCY) -1 = 0.618 -1 = -0.382 -> ADCS<5:0> = 0
    //TAD = (ADCS<5:0> + 1)*TCY/2 = 250 ns > TAD_min
    ADCON3 = 0x0000;

    // conversão usando amostragem no MUX A, no canal 0, na entrada AN8, Tensão de refer?ncia AVSS
    ADCHS = 0x0000;
    ADCHSbits.CH0SA = 0b1000;
    ADCHSbits.CH0NA = 0b0;

    // sem varredura das entradas analógicas
    ADCSSL = 0;

    ADCON1bits.ADON = 1; // habilitando conversão

    return 0;
}

float roundf(float entrada){
  int entrada_t = (int) entrada;
  float dec = (10*entrada) - (10*entrada_t);
  if (dec >= 5.0){
    return (float) (entrada_t+1);
  }
  if (dec < 5.0) {
    return (float) entrada_t;
  }
  return 0.0;
}

uint16_t convert_ad(int delay_adc_ms){
    __delay_ms(delay_adc_ms);
    ADCON1bits.SAMP = 0;
    while (!ADCON1bits.DONE);
    adc_out = ADCBUF0;
    return adc_out;
}

float converte_bin_current(uint16_t corrente_bin){
  return (corrente_bin - (const_i_niveis/2))*(ice_max/(const_i_niveis/2));
}

// +10 V -> 0x00
// -10 V -> 0xff
// v_out -> converte_da
uint8_t converte_da(float v_max, float v_min, int bin_v_min, float v_out) {
    return (uint8_t)roundf(-bin_v_min*(v_out-v_max)/(v_max-v_min));
}

void atualiza_da_in (uint8_t v_bin) {
    LATF = v_bin & 0x7f;
    _LATD0 = v_bin >> 7;
    return;
}

void atualiza_sh (int delay_sh_ms) {
    _LATD2 = 1;
    __delay_ms(delay_sh_ms);
    _LATD2 = 0;
    return;
}

void vbe_set(float vbe_in, float v_max, float v_min, int bin_v_min, int delay_sh_ms) {
    vbe_bin = converte_da(v_max,v_min,bin_v_min,vbe_in);
    atualiza_da_in(vbe_bin);
    atualiza_sh(delay_sh_ms);
    return;
}

void vce_set(float  vce_in, float  v_max, float  v_min, int bin_v_min) {
    //atualiza o valor binário de vce_bin para a representação binária equivalente da tensão Vce desejada
    vce_bin = converte_da(v_max,v_min,bin_v_min,vce_in);
    //atualiza o sinal digital da entrada do conversor D/A para a representação bináira da tensão Vce desejada
    atualiza_da_in(vce_bin);
    return;
}

void UART1_setup(){
  U1MODE = 0x0000;
  U1STA = 0X0000;
  // baud rate
  // U1BRG = ((FCY/baud_rate_desired)/16) - 1;
  U1BRG = 0b1001;
  // alternative I/O pins
  U1MODEbits.ALTIO =  0b1; // alternative I/O pins, U1ATX=15, U1ARX=16
  // data length and parity
  // U1MODEbits.PDSEL = 0b00; // 8 bits, no parity
  // stop bits
  // U1MODEbits.STSEL = 0; // 1 stop bit
  // Tx/Rx Interrupt enable and priority
  // Interrupt Flag Status
  IFS0bits.U1TXIF = 0;
  IFS0bits.U1RXIF = 0;
  // Interrupt Enable Control register
  IEC0bits.U1TXIE = 0;
  IEC0bits.U1RXIE = 0;
  // Interrupt Priority Control
  IPC2bits.U1TXIP = 0b100;
  IPC2bits.U1RXIP = 0b100;
  // Transmission interrupt mode
  // USTAbits.UTXISEL = 0; // at least one open character in transmit buffer
  // Receiver interrupt mod
  // U1STAbits.URXISEL = 0b0; // when a character is received
  U1MODEbits.USIDL = 1;
  // enable UART for transmission
  U1MODEbits.UARTEN = 1;
  U1STAbits.UTXEN = 0b1;
}

void UART1_tx_char(unsigned char tx_char){
    while(U1STAbits.UTXBF);
    U1TXREG = tx_char;
    return;
}

void UART1_tx_str(char *tx_str){
    int i = 0;
    while(tx_str[i]!=0){
        UART1_tx_char(tx_str[i]);
        i++;
    }
    return;
}

char UART1_rx_char(){
    char output_char;
    while(!U1STAbits.URXDA){
    }
    output_char = U1RXREG;
    return output_char;
}

char* UART1_rx_str(){
    memset(input_str,0,sizeof(input_str));
    int i = 0;
    input_str[0] = UART1_rx_char();
    while(1){
        i++;
        input_str[i] = UART1_rx_char();
        if(input_str[i]==0x0a){
            break;
        }
    }

    return input_str;
}

void data_in(){ // recebe configurações de varredura da GUI
  char buf[50];
  char* p;
  char* data_str;
  data_str = UART1_rx_str();
  strcpy(buf,data_str);
  // buf = "terminais*vbe_min*vbe_max*vbe_step"
  p = strtok(buf,"*");
  terminais = atoi(p);
  p = strtok(NULL,"*");
  vbe_min = atof(p);
  p = strtok(NULL,"*");
  vbe_max = atof(p);
  p = strtok(NULL,"*");
  vbe_step = atof(p);

  return;
}

int sweep_setup(void){
  // executa configuração inicial de varredura
  data_in();
  vbe = vbe_min;
  vce = v_min;
  vbe_set(vbe, v_max, v_min, bin_v_min, delay_sh_ms);
  vce_set(vce, v_max, v_min, bin_v_min);
  return 0;
}

// int data_out(){
//   sprintf(output_str,"Vce*");
//   UART1_tx_str(output_str);
//   int i = 0;
//   for (i = 0; i <= const_v_niveis; i++){
//     floattoint_str(vce_vec[i],vce_str);
//     strcat(vce_str,"*");
//     UART1_rx_str(vce_str);
//   }
//   sprintf(output_str,"Ice*");
//   UART1_tx_str(output_str);
//   for (i = 0; i <= const_v_niveis; i++){
//     floattoint_str(amostra_ad_out[i],ice_str);
//     strcat(ice_str,"*");
//     UART1_rx_str(ice_str);
//   }
//   sprintf(output_str,"\n");
//   UART1_tx_str(output_str);
//   return 0;
// }

int sweep_run(void){
  int i = 0;
  for (i = 0; i <= const_v_niveis; i++){
    vce_set(vce_vec[i], v_max, v_min, bin_v_min);
    amostra_ad_out = converte_bin_current(convert_ad(delay_adc_ms));
    sprintf(output_str,"%.2f\t%.2f*",vce_vec[i],amostra_ad_out);
  }
  sprintf(output_str,"\n");
  UART1_tx_str(output_str);

  return 0;
}


int trace(void){
  sweep_setup();
  if(terminais == 3){
      int i = 0;
    for (i = 0; i <= (vbe_max - vbe_min)/vbe_step; i++){
      curva = i+1;
      vbe = vbe_min + i*vbe_step;
      floattoint_str(vbe,vbe_str); // void floattoint_str(float f, char* int_str)
      sprintf(output_str,"Curva*%i*Vbe*%s*Vce*\t*Ice",curva,vbe_str);
      UART1_tx_str(output_str);
      vbe_set(vbe, v_max, v_min, bin_v_min, delay_sh_ms);
      sweep_run();
    }
  }
  if(terminais == 2){
    curva = 1;
    vbe = 0.00;
    floattoint_str(vbe,vbe_str); // void floattoint_str(float f, char* int_str)
    sprintf(output_str,"Curva*%i*Vbe*%s*Vce*\t*Ice",curva,vbe_str);
    UART1_tx_str(output_str);
    vbe_set(vbe, v_max, v_min, bin_v_min, delay_sh_ms);
    sweep_run();
  }
  return 0;
}



int main(){
  DAC_setup();
  blink();
  ADC_setup();
  blink();
  UART1_setup();
  blink();

  while(1){
    trace();
    UART1_tx_str("!");
  }

  return 0;
}
