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

// Constantes para convers�o A/D e D/A
#define const_v_max 10.0
#define const_v_min -10.0
#define const_v_niveis 0x100 // 8 bits de convers�o D/A
#define const_bin_v_min 0xff // valor bin�rio da tens�o de sa�da mais negativa para circuito em escada R-2R com amplificador inversor
// #define const_bin_v_max 0x00
#define const_i_max 20.0 // 40 mA
#define const_i_min -20.0 // 40 mA
#define const_i_niveis 0x3ff // 10 bits de convers�o A/D


// pino RB8 como sinal anal�gico
#define ADPCFG = 0xFEFF


// Vari�veis para comunica��o serial
unsigned int baud_rate_desired = 9600;
char input_char;
char input_str[50];
char output_str[100];

// Vari�veis para convers�o A/D e D/A
uint16_t adc_out = 0;
double vce = 0.0;
double vce_vec[const_v_niveis];
double vbe = 0.0;
double vbe_min = 0.0;
double vbe_max = 0.0;
double vbe_step = 0.0;
uint8_t vce_bin = 0;
uint8_t vbe_bin = 0;
double ice_max = const_i_max;
double ice_min = const_i_min;
double i_niveis = const_i_niveis;

double v_max = const_v_max;
double v_min = const_v_min;
int bin_v_min = const_bin_v_min ;
//int bin_v_max = const_bin_v_max;
double v_step;// = (const_v_max - const_v_min)/const_bin_v_min;

int delay_sh_ms = 100;
int delay_adc_ms = 10;

int varredura_completa = 0;
double amostra_ad_out;
int terminais = 2;
int curva = 1;

void blink(){
  _LATE0 = 0;
  __delay_ms(100);
  _LATE0 = 1;
  __delay_ms(100);
  _LATE0 = 0;
}

int DAC_setup(void){
    v_step = (v_max - v_min)/bin_v_min;
    int i = 0;
    for (i = 0 ; i <= const_v_niveis; i++){
      vce_vec[i] = v_min + (i*v_step);
    }
//    vce_vec[const_v_niveis] = v_min + (const_v_niveis*v_step);
//    // configura��o de ports
//    // RD e RF como sa�da digital
//    // RF0-6,RD0: sa�da digital para controle do conversor d/a
//    // RD2: controle do amostrador/retentor (0 => amostrando, 1 => retendo)
//    TRISD = 0;
//    TRISF = 0;
//    LATD = 0;
//    LATF = 0;

    return 0;
}

int ADC_setup(void){
    // pino RB8 como entrada
    _TRISB8 = 1;

    // configurando convers�o A/D
    ADCON1bits.ADON = 0; // desabilitando convers�o
    ADCON1bits.ASAM = 1; // habilita autosample
    ADCON1bits.FORM = 0b00; // sa�da em formato de n�mero inteiro sem sinal

    ADCON2 = 0; // tens?es de refer?ncia AVDD e AVSS, buffer de 16 bits

    //TAD_min >= 154 ns
    //TCY = 1/FCY = 500 ns
    //ADCS<5:0> = 2*(TAD_min/TCY) -1 = 0.618 -1 = -0.382 -> ADCS<5:0> = 0
    //TAD = (ADCS<5:0> + 1)*TCY/2 = 250 ns > TAD_min
    ADCON3 = 0x0000;

    // convers�o usando amostragem no MUX A, no canal 0, na entrada AN8, Tens�o de refer?ncia AVSS
    ADCHS = 0x0000;
    ADCHSbits.CH0SA = 0b1000;
    ADCHSbits.CH0NA = 0b0;

    // sem varredura das entradas anal�gicas
    ADCSSL = 0;

    ADCON1bits.ADON = 1; // habilitando convers�o

    return 0;
}

double roundf(double entrada){
  int entrada_t = (int) entrada;
  double dec = (10*entrada) - (10*entrada_t);
  if (dec >= 5.0){
    return (double) (entrada_t+1);
  }
  if (dec < 5.0) {
    return (double) entrada_t;
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

double converte_bin_current(uint16_t corrente_bin){
  return (((double)(corrente_bin)) - (i_niveis/2))*(ice_max/(i_niveis/2));
    // 5 V == -10 V == b1023
    // 2.5 V == 0 V == b512
    // 0 V == +10 V == b0
//    return ((((double)corrente_bin) - 512.0)/512.0)*10.0;
//    return corrente_bin;
}

// +10 V -> 0x00
// -10 V -> 0xff
// v_out -> converte_da
uint8_t converte_da(double v_max, double v_min, int bin_v_min, double v_out) {
    return (uint8_t)roundf(-bin_v_min*(v_out-v_max)/(v_max-v_min));
}

void atualiza_da_in (uint8_t v_bin) {
    LATF = v_bin & 0x7f;
    _LATD0 = v_bin >> 7;
    return;
}

void atualiza_sh (int delay_sh_ms) {
    _LATD2 = 1; //amostrando
    __delay_ms(delay_sh_ms);
    _LATD2 = 0; //retendo
    return;
}

void vbe_set(double vbe_in, double v_max, double v_min, int bin_v_min, int delay_sh_ms) {
    vbe_bin = converte_da(v_max,v_min,bin_v_min,vbe_in);
    atualiza_da_in(vbe_bin);
    atualiza_sh(delay_sh_ms);
    return;
}

void vce_set(double  vce_in, double  v_max, double  v_min, int bin_v_min) {
    //atualiza o valor bin�rio de vce_bin para a representa��o bin�ria equivalente da tens�o Vce desejada
    vce_bin = converte_da(v_max,v_min,bin_v_min,vce_in);
    //atualiza o sinal digital da entrada do conversor D/A para a representa��o bin�ira da tens�o Vce desejada
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

void data_in(){ // recebe configura��es de varredura da GUI
  char buf[50];
  char p[10];

   strcpy(buf,UART1_rx_str());
//  _LATD1 = 1;
   blink();
//  buf = "terminais*vbe_min*vbe_max*vbe_step"
  strcpy(p,(strtok(buf,"*")));
  terminais = atoi(p);
  strcpy(p,(strtok(NULL,"*")));
  vbe_min = (atoi(p))/10.0;
  strcpy(p,(strtok(NULL,"*")));
  vbe_max = atoi(p)/10.0;
  strcpy(p,(strtok(NULL,"*")));
  vbe_step = atoi(p)/10.0;
////  int dump;
////  dump = sprintf(output_str,"pino = %d; vbe = %f; vbe = %f; step = %f*", terminais, vbe_min, vbe_max, vbe_step);
////  UART1_tx_str(output_str);
}

int sweep_setup(void){
  // executa configura��o inicial de varredura
  data_in();
  vbe = vbe_min;
  vce = v_min;
  vbe_set(vbe, v_max, v_min, bin_v_min, delay_sh_ms);
  vce_set(vce, v_max, v_min, bin_v_min);

  return 0;
}


int sweep_run(void){
  int i = 0;
  int dump;
//  for (i = 0; i <= const_v_niveis; i++){
  for (i = 0; i <= const_v_niveis-1; i++){
    vce_set(vce_vec[i], v_max, v_min, bin_v_min);
    amostra_ad_out = converte_bin_current(convert_ad(delay_adc_ms));
    dump = sprintf(output_str,"%f\t%f*",vce_vec[i],amostra_ad_out);
    UART1_tx_str(output_str);
  }
  UART1_tx_str("\n");
  return 0;
}


int trace(void){
  sweep_setup();
  if(terminais == 3){
      int i = 0;
    for (i = 0; i <= (vbe_max - vbe_min)/vbe_step; i++){
      curva = i+1;
      vbe = vbe_min + i*vbe_step;
      sprintf(output_str,"Curva*%d*Vbe*%f*Vce\tIce*",curva,vbe);
      UART1_tx_str(output_str);
      vbe_set(vbe, v_max, v_min, bin_v_min, delay_sh_ms);
      sweep_run();
    }
  }
  if(terminais == 2){
    curva = 1;
    vbe = 0.00;
    sprintf(output_str,"Curva*%d*Vbe*%f*Vce\tIce*",curva,vbe);
    UART1_tx_str(output_str);
    vbe_set(vbe, v_max, v_min, bin_v_min, delay_sh_ms);
    sweep_run();
  }

  return 0;
}



int main(){
  DAC_setup();
    // configura��o de ports
    // RD e RF como sa�da digital
    // RF0-6,RD0: sa�da digital para controle do conversor d/a
    // RD2: controle do amostrador/retentor (0 => amostrando, 1 => retendo)
    TRISD = 0;
    _TRISE0 = 0;
    _LATE0 = 0;
    TRISF = 0;
    LATD = 0;
    LATF = 0;

  ADC_setup();

  UART1_setup();
  while(1){
    trace();
    vce_set(-10.0, v_max, v_min, bin_v_min);

    UART1_tx_str("!");
  }

  return 0;
}
