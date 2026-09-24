/* =========================================================
 * BAI 03 - DMA + BUTTON + USART1
 *
 * STM32F103
 *
 * PA0  : Button input pull-up
 * PA9  : USART1 TX
 * PA10 : USART1 RX
 *
 * USART1:
 *   Baud     = 9600
 *   Data     = 8 bit
 *   Parity   = None
 *   Stop     = 1
 *
 * DMA1 Channel 4:
 *   Memory -> USART1_DR
 *
 * Khi PA0 duoc noi voi GND:
 *
 * DCDT08802:BTN:1
 * DCDT08802:BTN:2
 * DCDT08802:BTN:3
 *
 * ========================================================= */


/* =========================================================
 * RCC
 * ========================================================= */

#define RCC_AHBENR    (*(volatile unsigned int *)0x40021014)
#define RCC_APB2ENR   (*(volatile unsigned int *)0x40021018)


/* =========================================================
 * GPIOA
 * ========================================================= */

#define GPIOA_CRL     (*(volatile unsigned int *)0x40010800)
#define GPIOA_CRH     (*(volatile unsigned int *)0x40010804)
#define GPIOA_IDR     (*(volatile unsigned int *)0x40010808)
#define GPIOA_ODR     (*(volatile unsigned int *)0x4001080C)


/* =========================================================
 * USART1
 * ========================================================= */

#define USART1_SR     (*(volatile unsigned int *)0x40013800)
#define USART1_DR     (*(volatile unsigned int *)0x40013804)
#define USART1_BRR    (*(volatile unsigned int *)0x40013808)
#define USART1_CR1    (*(volatile unsigned int *)0x4001380C)
#define USART1_CR3    (*(volatile unsigned int *)0x40013814)


/* =========================================================
 * DMA1
 * ========================================================= */

#define DMA1_ISR      (*(volatile unsigned int *)0x40020000)
#define DMA1_IFCR     (*(volatile unsigned int *)0x40020004)


/* =========================================================
 * DMA1 CHANNEL 4
 *
 * DMA1 Channel 4 = USART1_TX
 * ========================================================= */

#define DMA1_CCR4     (*(volatile unsigned int *)0x40020044)
#define DMA1_CNDTR4   (*(volatile unsigned int *)0x40020048)
#define DMA1_CPAR4    (*(volatile unsigned int *)0x4002004C)
#define DMA1_CMAR4    (*(volatile unsigned int *)0x40020050)


/* =========================================================
 * DMA CONTROL BITS
 * ========================================================= */

/* Channel Enable */
#define DMA_CCR_EN    (1 << 0)

/* Memory Increment */
#define DMA_CCR_MINC  (1 << 7)

/* Direction: Memory -> Peripheral */
#define DMA_CCR_DIR   (1 << 4)


/* =========================================================
 * DMA CHANNEL 4 FLAGS
 * ========================================================= */

/*
 * Channel 4:
 *
 * GIF4  = bit 12
 * TCIF4 = bit 13
 * HTIF4 = bit 14
 * TEIF4 = bit 15
 */

#define DMA_TCIF4     (1 << 13)
#define DMA_CGIF4     (1 << 12)


/* =========================================================
 * DELAY
 * ========================================================= */

static void delay(volatile unsigned int t)
{
    while (t--)
    {
        __asm volatile ("nop");
    }
}


/* =========================================================
 * USART1 INIT
 *
 * Clock = 8 MHz
 * Baud  = 9600
 * 8N1
 * ========================================================= */

static void USART1_Init(void)
{
    /*
     * Enable GPIOA clock
     *
     * IOPAEN = APB2ENR bit 2
     */
    RCC_APB2ENR |= (1 << 2);


    /*
     * Enable USART1 clock
     *
     * USART1EN = APB2ENR bit 14
     */
    RCC_APB2ENR |= (1 << 14);


    /*
     * PA9 = USART1_TX
     *
     * MODE9 = 11
     * CNF9  = 10
     *
     * => Alternate Function Push-Pull
     * => 50 MHz
     */

    GPIOA_CRH &= ~(0xF << 4);
    GPIOA_CRH |=  (0xB << 4);


    /*
     * PA10 = USART1_RX
     *
     * MODE10 = 00
     * CNF10  = 01
     *
     * => Floating input
     */

    GPIOA_CRH &= ~(0xF << 8);
    GPIOA_CRH |=  (0x4 << 8);


    /*
     * HSI clock = 8 MHz
     *
     * Baud = 9600
     *
     * BRR = 0x0341
     */

    USART1_BRR = 0x0341;


    /*
     * USART1_CR1:
     *
     * UE = bit 13
     * TE = bit 3
     * RE = bit 2
     */

    USART1_CR1 =
        (1 << 13) |
        (1 << 3)  |
        (1 << 2);


    /*
     * Enable USART1 TX DMA
     *
     * DMAT = CR3 bit 7
     */

    USART1_CR3 |= (1 << 7);
}


/* =========================================================
 * PA0 INIT
 *
 * PA0 = INPUT PULL-UP
 * ========================================================= */

static void PA0_Init(void)
{
    /*
     * Clear PA0 configuration
     */

    GPIOA_CRL &= ~(0xF << 0);


    /*
     * PA0:
     *
     * MODE = 00
     * CNF  = 10
     *
     * Input pull-up / pull-down
     */

    GPIOA_CRL |= (0x8 << 0);


    /*
     * Select PULL-UP
     */

    GPIOA_ODR |= (1 << 0);
}


/* =========================================================
 * DMA1 INIT
 * ========================================================= */

static void DMA1_Init(void)
{
    /*
     * Enable DMA1 clock
     *
     * DMA1EN = RCC_AHBENR bit 0
     */

    RCC_AHBENR |= (1 << 0);


    /*
     * Disable DMA Channel 4
     */

    DMA1_CCR4 = 0;


    /*
     * USART1 data register
     */

    DMA1_CPAR4 = (unsigned int)&USART1_DR;


    /*
     * Clear DMA Channel 4 flags
     */

    DMA1_IFCR = DMA_CGIF4;
}


/* =========================================================
 * DMA SEND STRING
 * ========================================================= */

static void DMA_SendString(const char *text)
{
    unsigned int len = 0;


    /*
     * Calculate string length
     */

    while (text[len] != '\0')
    {
        len++;
    }


    /*
     * Disable DMA Channel 4
     */

    DMA1_CCR4 = 0;


    /*
     * Clear old DMA flags
     */

    DMA1_IFCR = DMA_CGIF4;


    /*
     * Peripheral address
     *
     * USART1_DR
     */

    DMA1_CPAR4 = (unsigned int)&USART1_DR;


    /*
     * Memory address
     *
     * Address of string
     */

    DMA1_CMAR4 = (unsigned int)text;


    /*
     * Number of bytes
     */

    DMA1_CNDTR4 = len;


    /*
     * Configure DMA:
     *
     * MINC = 1
     * Memory address increases
     *
     * DIR = 1
     * Memory -> Peripheral
     */

    DMA1_CCR4 =
        DMA_CCR_MINC |
        DMA_CCR_DIR;


    /*
     * Enable DMA Channel 4
     */

    DMA1_CCR4 |= DMA_CCR_EN;


    /*
     * Wait for Transfer Complete
     *
     * TCIF4 = DMA transfer completed
     */

    while (!(DMA1_ISR & DMA_TCIF4))
    {
    }


    /*
     * Disable DMA Channel 4
     */

    DMA1_CCR4 &= ~DMA_CCR_EN;


    /*
     * Clear Transfer Complete flag
     */

    DMA1_IFCR = DMA_TCIF4;
}


/* =========================================================
 * SEND ONE CHARACTER USING DMA
 * ========================================================= */

static void DMA_SendChar(char c)
{
    char buffer[2];


    buffer[0] = c;
    buffer[1] = '\0';


    DMA_SendString(buffer);
}


/* =========================================================
 * SEND NUMBER USING DMA
 *
 * Example:
 *
 * 1   -> "1"
 * 12  -> "12"
 * 123 -> "123"
 * ========================================================= */

static void DMA_SendNumber(unsigned int number)
{
    char buffer[12];

    unsigned int i = 0;


    /*
     * Special case: number = 0
     */

    if (number == 0)
    {
        DMA_SendChar('0');

        return;
    }


    /*
     * Convert number to ASCII
     *
     * Example:
     *
     * 123
     *
     * buffer:
     * '3'
     * '2'
     * '1'
     */

    while (number > 0)
    {
        buffer[i] =
            '0' + (number % 10);

        i++;

        number /= 10;
    }


    /*
     * Send in reverse order
     *
     * 123:
     *
     * '1'
     * '2'
     * '3'
     */

    while (i > 0)
    {
        i--;

        DMA_SendChar(buffer[i]);
    }
}


/* =========================================================
 * MAIN
 * ========================================================= */

int main(void)
{
    unsigned int button_count = 0;


    /* Initialize USART1 */

    USART1_Init();


    /* Initialize PA0 */

    PA0_Init();


    /* Initialize DMA1 */

    DMA1_Init();


    /*
     * Small startup delay
     */

    delay(500000);


    /*
     * Startup message
     */

    DMA_SendString(
        "DCDT08802:DMA:READY\r\n"
    );


    /* =====================================================
     * MAIN LOOP
     * ===================================================== */

    while (1)
    {
        /*
         * PA0 = 0
         *
         * Button pressed
         * or PA0 connected to GND
         */

        if ((GPIOA_IDR & (1 << 0)) == 0)
        {
            /*
             * Increase button counter
             */

            button_count++;


            /*
             * Send:
             *
             * DCDT08802:BTN:
             */

            DMA_SendString(
                "DCDT08802:BTN:"
            );


            /*
             * Send button number
             */

            DMA_SendNumber(button_count);


            /*
             * New line
             */

            DMA_SendString(
                "\r\n"
            );


            /*
             * Wait until button is released
             *
             * This prevents one long press
             * from being counted many times.
             */

            while ((GPIOA_IDR & (1 << 0)) == 0)
            {
            }


            /*
             * Debounce delay
             */

            delay(100000);
        }
    }
}
