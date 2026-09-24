CC = arm-none-eabi-gcc

CFLAGS = -mcpu=cortex-m3 \
         -mthumb \
         -O0 \
         -Wall \
         -ffreestanding

LDFLAGS = -nostdlib \
          -T stm32f103c8.ld

TARGET = Bai02_W25Q64

all: $(TARGET).bin

$(TARGET).elf: main.o startup.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lgcc

$(TARGET).bin: $(TARGET).elf
	arm-none-eabi-objcopy -O binary $< $@

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

startup.o: startup.s
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.elf *.bin
