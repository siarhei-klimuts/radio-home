/**
 * MCU = atmega8a
 * Fuse bits:
 * Low 0xe4 int
 * Low 0xff ext
 * High 0xd9
 * Ext 0xff
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "../lib/nRF24L01.h"

#define CSN_PORT PORTB
#define CSN_PIN PORTB2
#define CE_PORT PORTB
#define CE_PIN PORTB1

#define NRF_DATA_LENGTH 32
#define SET_REGISTER_DELAY 100
#define FREQ_DEVIDER 10

#define SETBIT(x, y) x |= (1 << y)
#define CLEARBIT(x, y) x &= (~(1 << y))

#define CSN_LOW() CLEARBIT(CSN_PORT, CSN_PIN)
#define CSN_HIGH() SETBIT(CSN_PORT, CSN_PIN)
#define CE_LOW() CLEARBIT(CE_PORT, CE_PIN)
#define CE_HIGH() SETBIT(CE_PORT, CE_PIN)

volatile uint8_t bufferCounter = 0;
volatile uint8_t currentBuffer = 0;
volatile int readyBuffer = -1;
uint8_t buffer[2][NRF_DATA_LENGTH];

void initMSPI() {
    DDRB |= (1<<PB3)|(1<<PB5)|(1<<CSN_PIN)|(1<<CE_PIN);
    SPCR = (1<<SPE)|(1<<MSTR);
}

uint8_t writeMSPI(uint8_t data) {
    /* Start transmission */
    SPDR = data;
    /* Wait for transmission complete */
    while(!(SPSR & (1<<SPIF)));

    /* Return Data Register */
    return SPDR;
}

void initNrfRegister(uint8_t reg) {
    CSN_LOW();
    _delay_us(SET_REGISTER_DELAY);
    writeMSPI(reg);
    _delay_us(SET_REGISTER_DELAY);
}

void getNrfReceivedData(uint8_t *data) {
    initNrfRegister(R_RX_PAYLOAD);

    int i;
    for(i = 0; i < NRF_DATA_LENGTH; i++) {
        data[i] = writeMSPI(NOP);
        _delay_us(SET_REGISTER_DELAY);
    }

    CSN_HIGH();
}

void setNrfTransmitData(uint8_t *data, uint8_t data_length) {
    initNrfRegister(W_TX_PAYLOAD);
   
    uint8_t i;
    for (i = 0; i < data_length; i++) {
        writeMSPI(data[i]);
        _delay_us(SET_REGISTER_DELAY);
    }

    CSN_HIGH();
}

void setNrfRegister(uint8_t reg, uint8_t val, uint8_t count) {
    initNrfRegister(W_REGISTER + reg);

    int i;
    for(i = 0; i < count; i++) {
        writeMSPI(val);
        _delay_us(SET_REGISTER_DELAY);
    }

    CSN_HIGH();
}

uint8_t getNfrRegister(uint8_t reg) {
    initNrfRegister(R_REGISTER + reg);

    uint8_t value = writeMSPI(NOP);
    _delay_us(SET_REGISTER_DELAY);

    CSN_HIGH();

    return value;
}

void initNrf() {
    CSN_HIGH();//CSN to high to disable nrf
    CE_LOW();//CE to low to nothing to send/receive

    _delay_ms(100);

    // Disabled auto-acknowledgements
    setNrfRegister(EN_AA, 0x00, 1);

    // Number of retries and delay
    // "2": 750us delay; "F": 15 retries
    setNrfRegister(SETUP_RETR, 0x2F, 1);

    // data pipe 0
    setNrfRegister(EN_RXADDR, 0x01, 1);

    // 5 bytes RF_Address length
    setNrfRegister(SETUP_AW, 0x03, 1);

    // 2.401GHz
    setNrfRegister(RF_CH, 0x01, 1);

    // power and data speed
    // 00000111 bit[3]=0 1Mbps - longer rage; bit[2-1] power mode (11=-0dB; 00=-8dB)
    setNrfRegister(RF_SETUP, 0x07, 1);

    // Receiver address
    setNrfRegister(RX_ADDR_P0, 0x12, 5);
    // Transmitter address
    setNrfRegister(TX_ADDR, 0x12, 5);

    // Payload length setup
    setNrfRegister(RX_PW_P0, NRF_DATA_LENGTH, 1);

    //Primary transmitter (PRIM_RX = 0); Power up (PWR_UP = 1); CRC disabled (EN_CRC = 0)
    setNrfRegister(CONFIG, 0b00010010, 1);

    _delay_ms(100);
}

void print(uint8_t reg) {
    PORTD = getNfrRegister(reg);
    _delay_ms(500);
    PORTD = 0;
    _delay_ms(500);
}

void initPWM() {
    DDRB |= (1 << DDB3);
    // (0) (1(xx)1)-Fast PWM; (01)-OC2 non-inverting mode; (001)-no prescaling
    // TCCR2 = 0b01101001;
    // (0) (1(xx)1)-Fast PWM; (00)-OC2 pin disconected; (001)-no prescaling
    TCCR2 = 0b01001001;
    
    // Enable ovf interrupt
    TIMSK |= (1<<TOIE2);
}

void initADC() {
    PORTC = 0xff;
    // ADCSRA = 0b10000101;
    //1 ADEN - ADC enabled
    //1 ADSC - Start conversion
    //1 ADFR - Free running
    //0 ADIF - interrupt flag
    //0 ADIE - interrupt enable
    //101 ADPS[2:0] - division factor 32
    ADCSRA = 0b11100101;
    ADMUX = 0b11100101;
}

main() {
    // debug output port
    DDRD = 0xff;

    initMSPI();
    initNrf();

    initADC();
    initPWM();

    print(STATUS);

    sei();

    CE_HIGH();
    while(1) {
        if (readyBuffer != -1) {
            int b = readyBuffer;
            readyBuffer = -1;
            setNrfTransmitData(buffer[b], NRF_DATA_LENGTH);
        }
    }

    return 1;
}

uint8_t ovfCounter = 0;
ISR(TIMER2_OVF_vect) {
    ovfCounter++;
    if (ovfCounter > FREQ_DEVIDER) {
        ovfCounter = 0;
        
        buffer[currentBuffer][bufferCounter] = ADCH;

        bufferCounter++;
        if (bufferCounter == NRF_DATA_LENGTH) {
            bufferCounter = 0;

            readyBuffer = currentBuffer;
            currentBuffer = (currentBuffer == 0 ? 1 : 0);
        }
    }
}
