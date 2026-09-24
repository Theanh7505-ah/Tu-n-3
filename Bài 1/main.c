#include <stdint.h>

/* =========================================================
 * STM32F103
 * BMP280 I2C
 *
 * I2C:
 * PB6 = SCL
 * PB7 = SDA
 *
 * UART:
 * PA9  = TX
 * PA10 = RX
 *
 * BMP280 address = 0x76
 * =========================================================
 */

#define RCC_BASE        0x40021000UL
#define GPIOA_BASE      0x40010800UL
#define GPIOB_BASE      0x40010C00UL
#define USART1_BASE     0x40013800UL
#define I2C1_BASE       0x40005400UL
#define FLASH_BASE      0x40022000UL

/* RCC */
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR        (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x1C))

/* FLASH */
#define FLASH_ACR       (*(volatile uint32_t *)(FLASH_BASE + 0x00))

/* GPIO */
#define GPIOA_CRH       (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOB_CRL       (*(volatile uint32_t *)(GPIOB_BASE + 0x00))

/* USART1 */
#define USART1_SR       (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_DR       (*(volatile uint32_t *)(USART1_BASE + 0x04))
#define USART1_BRR      (*(volatile uint32_t *)(USART1_BASE + 0x08))
#define USART1_CR1      (*(volatile uint32_t *)(USART1_BASE + 0x0C))

/* I2C1 */
#define I2C1_CR1        (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2        (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_OAR1       (*(volatile uint32_t *)(I2C1_BASE + 0x08))
#define I2C1_DR         (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_SR1        (*(volatile uint32_t *)(I2C1_BASE + 0x14))
#define I2C1_SR2        (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_CCR        (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TRISE      (*(volatile uint32_t *)(I2C1_BASE + 0x20))

#define BMP280_ADDR     0x76

/* =========================================================
 * BMP280 calibration data
 * =========================================================
 */

static uint16_t dig_T1;
static int16_t  dig_T2;
static int16_t  dig_T3;

static uint16_t dig_P1;
static int16_t  dig_P2;
static int16_t  dig_P3;
static int16_t  dig_P4;
static int16_t  dig_P5;
static int16_t  dig_P6;
static int16_t  dig_P7;
static int16_t  dig_P8;
static int16_t  dig_P9;

static int32_t t_fine;

/* =========================================================
 * Delay
 * =========================================================
 */

static void delay(volatile uint32_t count)
{
    while (count--)
    {
        __asm volatile ("nop");
    }
}

/* =========================================================
 * Clock: HSE 8 MHz -> PLL x9 -> 72 MHz
 * =========================================================
 */

static void clock_init(void)
{
    /* Enable HSE */
    RCC_CR |= (1 << 16);

    while (!(RCC_CR & (1 << 17)));

    /* Flash wait state */
    FLASH_ACR = (1 << 4) | 2;

    /* APB1 = HCLK / 2 = 36 MHz */
    RCC_CFGR = 0;
    RCC_CFGR |= (4 << 8);

    /* PLL x9 */
    RCC_CFGR |= (7 << 18);

    /* HSE as PLL source */
    RCC_CFGR |= (1 << 16);

    /* Enable PLL */
    RCC_CR |= (1 << 24);

    while (!(RCC_CR & (1 << 25)));

    /* Switch SYSCLK to PLL */
    RCC_CFGR |= 2;

    while (((RCC_CFGR >> 2) & 3) != 2);
}

/* =========================================================
 * UART1 9600 baud
 * PA9  = TX
 * PA10 = RX
 * =========================================================
 */

static void uart_init(void)
{
    /* GPIOA clock */
    RCC_APB2ENR |= (1 << 2);

    /* USART1 clock */
    RCC_APB2ENR |= (1 << 14);

    /* PA9 TX = Alternate Function Push Pull */
    GPIOA_CRH &= ~(0xF << 4);
    GPIOA_CRH |=  (0xB << 4);

    /* PA10 RX = Floating input */
    GPIOA_CRH &= ~(0xF << 8);
    GPIOA_CRH |=  (0x4 << 8);

    /* 72 MHz / 9600 */
    USART1_BRR = 7500;

    /* UE + TE + RE */
    USART1_CR1 = (1 << 13) |
                 (1 << 3)  |
                 (1 << 2);
}

static void uart_send_char(char c)
{
    while (!(USART1_SR & (1 << 7)));

    USART1_DR = c;
}

static void uart_send_string(const char *s)
{
    while (*s)
    {
        uart_send_char(*s++);
    }
}

static void uart_send_uint(uint32_t value)
{
    char buffer[10];
    int i = 0;

    if (value == 0)
    {
        uart_send_char('0');
        return;
    }

    while (value > 0)
    {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0)
    {
        uart_send_char(buffer[--i]);
    }
}

static void uart_send_int(int32_t value)
{
    if (value < 0)
    {
        uart_send_char('-');
        value = -value;
    }

    uart_send_uint((uint32_t)value);
}

/* =========================================================
 * I2C1
 *
 * PB6 = SCL
 * PB7 = SDA
 * 100 kHz
 * =========================================================
 */

static void i2c_init(void)
{
    /* GPIOB clock */
    RCC_APB2ENR |= (1 << 3);

    /* I2C1 clock */
    RCC_APB1ENR |= (1 << 21);

    /*
     * PB6/PB7:
     * Alternate Function Open Drain
     */
    GPIOB_CRL &= ~(0xFF << 24);
    GPIOB_CRL |=  (0xFF << 24);

    /*
     * APB1 = 36 MHz
     */
    I2C1_CR2 = 36;

    /*
     * Own address
     */
    I2C1_OAR1 = (1 << 14);

    /*
     * 100 kHz:
     * CCR = 36MHz / (2 * 100kHz)
     *     = 180
     */
    I2C1_CCR = 180;

    /*
     * TRISE = 36 + 1
     */
    I2C1_TRISE = 37;

    /* Enable I2C */
    I2C1_CR1 = 1;

    /* Enable ACK */
    I2C1_CR1 |= (1 << 10);
}

/* =========================================================
 * I2C START
 * =========================================================
 */

static int i2c_start(void)
{
    uint32_t timeout = 100000;

    I2C1_CR1 |= (1 << 8);

    while (!(I2C1_SR1 & 1))
    {
        if (--timeout == 0)
            return 0;
    }

    return 1;
}

/* =========================================================
 * I2C STOP
 * =========================================================
 */

static void i2c_stop(void)
{
    I2C1_CR1 |= (1 << 9);
}

/* =========================================================
 * Send address
 * address includes R/W bit
 * =========================================================
 */

static int i2c_send_address(uint8_t address)
{
    uint32_t timeout = 100000;

    I2C1_DR = address;

    while (!(I2C1_SR1 & (1 << 1)))
    {
        if (I2C1_SR1 & (1 << 10))
        {
            I2C1_SR1 &= ~(1 << 10);
            return 0;
        }

        if (--timeout == 0)
            return 0;
    }

    /*
     * Clear ADDR flag
     */
    volatile uint32_t temp;

    temp = I2C1_SR1;
    temp = I2C1_SR2;

    (void)temp;

    return 1;
}

/* =========================================================
 * I2C WRITE BYTE
 * =========================================================
 */

static int i2c_write_byte(uint8_t data)
{
    uint32_t timeout = 100000;

    I2C1_DR = data;

    while (!(I2C1_SR1 & (1 << 2)))
    {
        if (I2C1_SR1 & (1 << 10))
        {
            I2C1_SR1 &= ~(1 << 10);
            return 0;
        }

        if (--timeout == 0)
            return 0;
    }

    return 1;
}

/* =========================================================
 * I2C READ ONE BYTE
 * =========================================================
 */

static uint8_t i2c_read_byte(void)
{
    uint32_t timeout = 100000;
    uint8_t data;

    /*
     * NACK after one byte
     */
    I2C1_CR1 &= ~(1 << 10);

    /*
     * STOP
     */
    I2C1_CR1 |= (1 << 9);

    while (!(I2C1_SR1 & (1 << 6)))
    {
        if (--timeout == 0)
            break;
    }

    data = (uint8_t)I2C1_DR;

    /*
     * Enable ACK again
     */
    I2C1_CR1 |= (1 << 10);

    return data;
}

/* =========================================================
 * BMP280 WRITE REGISTER
 * =========================================================
 */

static int bmp280_write_reg(uint8_t reg, uint8_t value)
{
    if (!i2c_start())
        return 0;

    if (!i2c_send_address((BMP280_ADDR << 1) | 0))
    {
        i2c_stop();
        return 0;
    }

    if (!i2c_write_byte(reg))
    {
        i2c_stop();
        return 0;
    }

    if (!i2c_write_byte(value))
    {
        i2c_stop();
        return 0;
    }

    i2c_stop();

    return 1;
}

/* =========================================================
 * BMP280 READ REGISTER
 * =========================================================
 */

static uint8_t bmp280_read_reg(uint8_t reg)
{
    uint8_t value = 0;

    if (!i2c_start())
        return 0;

    /*
     * Address + WRITE
     */
    if (!i2c_send_address((BMP280_ADDR << 1) | 0))
    {
        i2c_stop();
        return 0;
    }

    /*
     * Register address
     */
    if (!i2c_write_byte(reg))
    {
        i2c_stop();
        return 0;
    }

    /*
     * Repeated START
     */
    if (!i2c_start())
    {
        i2c_stop();
        return 0;
    }

    /*
     * Address + READ
     */
    if (!i2c_send_address((BMP280_ADDR << 1) | 1))
    {
        i2c_stop();
        return 0;
    }

    /*
     * Read one byte
     */
    value = i2c_read_byte();

    return value;
}

/* =========================================================
 * Read calibration data
 *
 * 0x88 -> temperature calibration
 * 0x8E -> pressure calibration
 * =========================================================
 */

static void bmp280_read_calibration(void)
{
    uint8_t lo;
    uint8_t hi;

    /* T1 */
    lo = bmp280_read_reg(0x88);
    hi = bmp280_read_reg(0x89);
    dig_T1 = ((uint16_t)hi << 8) | lo;

    /* T2 */
    lo = bmp280_read_reg(0x8A);
    hi = bmp280_read_reg(0x8B);
    dig_T2 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* T3 */
    lo = bmp280_read_reg(0x8C);
    hi = bmp280_read_reg(0x8D);
    dig_T3 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* P1 */
    lo = bmp280_read_reg(0x8E);
    hi = bmp280_read_reg(0x8F);
    dig_P1 = ((uint16_t)hi << 8) | lo;

    /* P2 */
    lo = bmp280_read_reg(0x90);
    hi = bmp280_read_reg(0x91);
    dig_P2 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* P3 */
    lo = bmp280_read_reg(0x92);
    hi = bmp280_read_reg(0x93);
    dig_P3 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* P4 */
    lo = bmp280_read_reg(0x94);
    hi = bmp280_read_reg(0x95);
    dig_P4 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* P5 */
    lo = bmp280_read_reg(0x96);
    hi = bmp280_read_reg(0x97);
    dig_P5 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* P6 */
    lo = bmp280_read_reg(0x98);
    hi = bmp280_read_reg(0x99);
    dig_P6 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* P7 */
    lo = bmp280_read_reg(0x9A);
    hi = bmp280_read_reg(0x9B);
    dig_P7 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* P8 */
    lo = bmp280_read_reg(0x9C);
    hi = bmp280_read_reg(0x9D);
    dig_P8 = (int16_t)(((uint16_t)hi << 8) | lo);

    /* P9 */
    lo = bmp280_read_reg(0x9E);
    hi = bmp280_read_reg(0x9F);
    dig_P9 = (int16_t)(((uint16_t)hi << 8) | lo);
}

/* =========================================================
 * Read raw temperature
 *
 * Registers:
 * 0xFA
 * 0xFB
 * 0xFC
 * =========================================================
 */

static int32_t bmp280_read_temperature_raw(void)
{
    uint32_t msb;
    uint32_t lsb;
    uint32_t xlsb;

    msb  = bmp280_read_reg(0xFA);
    lsb  = bmp280_read_reg(0xFB);
    xlsb = bmp280_read_reg(0xFC);

    return (int32_t)((msb << 12) |
                     (lsb << 4)  |
                     (xlsb >> 4));
}

/* =========================================================
 * Read raw pressure
 *
 * Registers:
 * 0xF7
 * 0xF8
 * 0xF9
 * =========================================================
 */

static int32_t bmp280_read_pressure_raw(void)
{
    uint32_t msb;
    uint32_t lsb;
    uint32_t xlsb;

    msb  = bmp280_read_reg(0xF7);
    lsb  = bmp280_read_reg(0xF8);
    xlsb = bmp280_read_reg(0xF9);

    return (int32_t)((msb << 12) |
                     (lsb << 4)  |
                     (xlsb >> 4));
}

/* =========================================================
 * Temperature compensation
 *
 * Return temperature x100
 *
 * Example:
 * 2843 = 28.43 C
 * =========================================================
 */

static int32_t bmp280_compensate_temperature(int32_t adc_T)
{
    int32_t var1;
    int32_t var2;
    int32_t T;

    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1)))
           * ((int32_t)dig_T2)) >> 11;

    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1))
              * ((adc_T >> 4) - ((int32_t)dig_T1)))
             >> 12)
             * ((int32_t)dig_T3)) >> 14;

    t_fine = var1 + var2;

    T = (t_fine * 5 + 128) >> 8;

    return T;
}

/* =========================================================
 * Pressure compensation
 *
 * Return pressure in Pa
 * =========================================================
 */

static uint32_t bmp280_compensate_pressure(int32_t adc_P)
{
    int64_t var1;
    int64_t var2;
    int64_t p;

    var1 = ((int64_t)t_fine) - 128000;

    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);

    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8)
         + ((var1 * (int64_t)dig_P2) << 12);

    var1 = (((((int64_t)1) << 47) + var1)
          * ((int64_t)dig_P1)) >> 33;

    if (var1 == 0)
        return 0;

    p = 1048576 - adc_P;

    p = (((p << 31) - var2) * 3125) / var1;

    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;

    var2 = (((int64_t)dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8)
      + (((int64_t)dig_P7) << 4);

    return (uint32_t)p;
}

/* =========================================================
 * Print temperature
 *
 * Input:
 * 2843 -> 28.43 C
 * =========================================================
 */

static void print_temperature(int32_t temperature)
{
    if (temperature < 0)
    {
        uart_send_char('-');
        temperature = -temperature;
    }

    uart_send_int(temperature / 100);
    uart_send_char('.');
    
    uart_send_char('0' + ((temperature / 10) % 10));
    uart_send_char('0' + (temperature % 10));

    uart_send_string(" C");
}

/* =========================================================
 * Print pressure
 *
 * Pressure is Pa
 *
 * Example:
 * 100782 Pa
 * -> 1007.82 hPa
 * =========================================================
 */

static void print_pressure(uint32_t pressure)
{
    uint32_t hpa_x100;

    hpa_x100 = pressure / 10;

    uart_send_uint(hpa_x100 / 100);
    uart_send_char('.');

    uart_send_char('0' + ((hpa_x100 / 10) % 10));
    uart_send_char('0' + (hpa_x100 % 10));

    uart_send_string(" hPa");
}

/* =========================================================
 * MAIN
 * =========================================================
 */

int main(void)
{
    uint8_t chip_id;

    int32_t adc_T;
    int32_t adc_P;

    int32_t temperature;
    uint32_t pressure;

    clock_init();
    uart_init();
    i2c_init();

    delay(1000000);

    uart_send_string("\r\n");
    uart_send_string("============================\r\n");
    uart_send_string("BMP280 SENSOR\r\n");
    uart_send_string("============================\r\n");

    /* Read Chip ID */
    chip_id = bmp280_read_reg(0xD0);

    uart_send_string("BMP280 ID = 0x");

    if (chip_id == 0x58)
    {
        uart_send_string("58\r\n");
        uart_send_string("BMP280 CONNECT OK\r\n");
    }
    else
    {
        uart_send_string("ERROR\r\n");
        uart_send_string("CHECK I2C WIRING\r\n");

        while (1)
        {
            delay(7200000);
        }
    }

    /*
     * Read calibration coefficients
     */
    bmp280_read_calibration();

    /*
     * CTRL_MEAS register = 0xF4
     *
     * Temperature oversampling = x1
     * Pressure oversampling    = x1
     * Normal mode
     *
     * 001 001 11
     * = 0x27
     */
    bmp280_write_reg(0xF4, 0x27);

    /*
     * CONFIG register = 0xF5
     *
     * Standby 1000 ms
     * Filter off
     */
    bmp280_write_reg(0xF5, 0xA0);

    uart_send_string("\r\n");
    uart_send_string("Reading sensor...\r\n");
    uart_send_string("\r\n");

    while (1)
    {
        /*
         * Wait for new measurement
         */
        delay(7200000);

        /*
         * Read raw temperature
         */
        adc_T = bmp280_read_temperature_raw();

        /*
         * Read raw pressure
         */
        adc_P = bmp280_read_pressure_raw();

        /*
         * Calculate temperature
         */
        temperature = bmp280_compensate_temperature(adc_T);

        /*
         * Calculate pressure
         */
        pressure = bmp280_compensate_pressure(adc_P);

        /*
         * Print temperature
         */
        uart_send_string("Temperature: ");
        print_temperature(temperature);
        uart_send_string("\r\n");

        /*
         * Print pressure
         */
        uart_send_string("Pressure: ");
        print_pressure(pressure);
        uart_send_string("\r\n");

        uart_send_string("----------------------------\r\n");
    }
}
