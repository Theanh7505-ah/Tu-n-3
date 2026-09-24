#define RCC_APB2ENR   (*(volatile unsigned int *)0x40021018)

#define GPIOA_CRL     (*(volatile unsigned int *)0x40010800)
#define GPIOA_IDR     (*(volatile unsigned int *)0x40010808)
#define GPIOA_ODR     (*(volatile unsigned int *)0x4001080C)

#define USART1_SR     (*(volatile unsigned int *)0x40013800)
#define USART1_DR     (*(volatile unsigned int *)0x40013804)
#define USART1_BRR    (*(volatile unsigned int *)0x40013808)
#define USART1_CR1    (*(volatile unsigned int *)0x4001380C)


/* =========================
   DELAY
   ========================= */

static void delay(volatile unsigned int t)
{
    while (t--)
    {
        __asm volatile("nop");
    }
}


/* =========================
   UART1
   ========================= */

static void USART1_Init(void)
{
    /* GPIOA clock */
    RCC_APB2ENR |= (1 << 2);

    /* USART1 clock */
    RCC_APB2ENR |= (1 << 14);

    /*
       PA9  = TX
       PA10 = RX
    */

    GPIOA_CRL = GPIOA_CRL;

    USART1_BRR = 0x0341;       // 9600 baud @ 8 MHz

    USART1_CR1 =
        (1 << 13) |            // UE
        (1 << 3)  |            // TE
        (1 << 2);              // RE
}


static void USART1_SendChar(char c)
{
    while (!(USART1_SR & (1 << 7)))
    {
    }

    USART1_DR = c;
}


static void USART1_SendString(const char *s)
{
    while (*s)
    {
        USART1_SendChar(*s);
        s++;
    }
}


static void USART1_SendHex(unsigned char x)
{
    const char hex[] = "0123456789ABCDEF";

    USART1_SendChar(hex[(x >> 4) & 0x0F]);
    USART1_SendChar(hex[x & 0x0F]);
}


/* =========================
   GPIO SPI
   =========================

   PA4 = CS
   PA5 = CLK
   PA6 = DO / MISO
   PA7 = DI / MOSI

   SPI MODE 0:
   CLK idle LOW
   gửi dữ liệu khi CLK LOW
   đọc MISO khi CLK HIGH
*/


static void GPIO_SPI_Init(void)
{
    /*
       PA4 = output push-pull
       PA5 = output push-pull
       PA7 = output push-pull
       PA6 = input floating
    */

    GPIOA_CRL &= ~(
        (0xF << 16) |
        (0xF << 20) |
        (0xF << 24) |
        (0xF << 28)
    );

    /*
       PA4 = 0011
       Output 10 MHz push-pull
    */
    GPIOA_CRL |= (0x3 << 16);

    /*
       PA5 = 0011
    */
    GPIOA_CRL |= (0x3 << 20);

    /*
       PA6 = 0100
       Input floating
    */
    GPIOA_CRL |= (0x4 << 24);

    /*
       PA7 = 0011
    */
    GPIOA_CRL |= (0x3 << 28);


    /* CS HIGH */
    GPIOA_ODR |= (1 << 4);

    /* CLK LOW */
    GPIOA_ODR &= ~(1 << 5);

    /* MOSI LOW */
    GPIOA_ODR &= ~(1 << 7);
}


/* =========================
   GPIO SPI TRANSFER
   ========================= */

static unsigned char SPI_GPIO_Transfer(unsigned char data)
{
    unsigned char rx = 0;
    int i;

    for (i = 7; i >= 0; i--)
    {
        /*
           Đưa bit MOSI ra
        */

        if (data & (1 << i))
            GPIOA_ODR |= (1 << 7);
        else
            GPIOA_ODR &= ~(1 << 7);

        delay(50);


        /*
           CLK HIGH
        */

        GPIOA_ODR |= (1 << 5);

        delay(50);


        /*
           Đọc MISO
        */

        rx <<= 1;

        if (GPIOA_IDR & (1 << 6))
        {
            rx |= 1;
        }


        /*
           CLK LOW
        */

        GPIOA_ODR &= ~(1 << 5);

        delay(50);
    }

    return rx;
}


/* =========================
   READ JEDEC ID
   ========================= */

static void W25Q_ReadID(void)
{
    unsigned char id1;
    unsigned char id2;
    unsigned char id3;


    /* CS LOW */

    GPIOA_ODR &= ~(1 << 4);

    delay(500);


    /*
       JEDEC ID command
    */

    SPI_GPIO_Transfer(0x9F);

    delay(500);


    /*
       Nhận 3 byte ID
    */

    id1 = SPI_GPIO_Transfer(0xFF);
    id2 = SPI_GPIO_Transfer(0xFF);
    id3 = SPI_GPIO_Transfer(0xFF);


    /* CS HIGH */

    GPIOA_ODR |= (1 << 4);

    delay(500);


    USART1_SendString("BITBANG ID = ");

    USART1_SendHex(id1);
    USART1_SendChar(' ');

    USART1_SendHex(id2);
    USART1_SendChar(' ');

    USART1_SendHex(id3);

    USART1_SendString("\r\n");
}


/* =========================
   MAIN
   ========================= */

int main(void)
{
    USART1_Init();

    GPIO_SPI_Init();

    delay(500000);

    USART1_SendString("\r\n");
    USART1_SendString("============================\r\n");
    USART1_SendString("W25Q64 GPIO SPI TEST\r\n");
    USART1_SendString("============================\r\n");

    while (1)
    {
        W25Q_ReadID();

        delay(2000000);
    }

    return 0;
}
