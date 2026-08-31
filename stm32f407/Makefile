# STM32F407 Makefile (GNU Arm Embedded Toolchain)
# Targets: all (slot_a+slot_b), bootloader, app_slot_a, app_slot_b

BUILD_DIR = build
CORE_DIR  = ../oled_cubemx

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CPU     = -mcpu=cortex-m4
FPU     = -mfpu=fpv4-sp-d16 -mfloat-abi=hard
MCU     = $(CPU) -mthumb $(FPU)
CFLAGS  = $(MCU) -Wall -O2 -g -DSTM32F407xx -DUSE_HAL_DRIVER
LDFLAGS = $(MCU) -Wl,--gc-sections -specs=nosys.specs -specs=nano.specs -u _printf_float

# ---- Include Paths ----
INC_APP = -Iinc \
          -I$(CORE_DIR)/Inc \
          -I$(CORE_DIR)/Drivers/STM32F4xx_HAL_Driver/Inc \
          -I$(CORE_DIR)/Drivers/CMSIS/Device/ST/STM32F4xx/Include \
          -I$(CORE_DIR)/Drivers/CMSIS/Include \
          -Ithirdparty/freertos/include \
          -Ithirdparty/freertos/portable/GCC/ARM_CM4F

INC_BOOT = $(INC_APP) -Iiap/inc

# ---- APP 源文件 ----
APP_SRC = src/ssd1306.c src/font.c src/display_mgr.c \
          src/i2c_drv.c src/uart_drv.c src/protocol.c \
          src/led_mgr.c src/key_drv.c src/iwdg_drv.c src/sys_config.c \
          src/sys_tick.c src/user_app.c src/menu_mgr.c src/menu_items.c \
          src/debug_console.c src/nr_micro_shell_core.c src/cli_cmds.c \
          src/app_fw_info.c src/ota_req.c src/app_ipc.c src/freertos_app.c \
          thirdparty/freertos/tasks.c thirdparty/freertos/queue.c \
          thirdparty/freertos/list.c thirdparty/freertos/event_groups.c \
          thirdparty/freertos/stream_buffer.c thirdparty/freertos/timers.c \
          thirdparty/freertos/portable/GCC/ARM_CM4F/port.c \
          thirdparty/freertos/portable/MemMang/heap_4.c

HAL_SRC = $(CORE_DIR)/Src/main.c $(CORE_DIR)/Src/stm32f4xx_it.c \
          $(CORE_DIR)/Src/stm32f4xx_hal_msp.c $(CORE_DIR)/Src/system_stm32f4xx.c

HAL_LIB = $(CORE_DIR)/Drivers/STM32F4xx_HAL_Driver/Src/*.c

STARTUP = $(CORE_DIR)/MDK-ARM/startup_stm32f407xx.s

# ---- Bootloader 源文件 ----
BOOT_SRC = iap/src/boot_main.c iap/src/boot_fw_info.c \
           iap/src/boot_flash.c iap/src/boot_proto.c \
           iap/src/boot_oled.c iap/src/system_stm32f4xx.c

BOOT_STARTUP = iap/startup/startup_stm32f407xx.s

# ---- Default ----
all: app_slot_a app_slot_b bootloader

# ---- Bootloader (ROM 0x08000000) ----
bootloader: $(BUILD_DIR)/bootloader.bin

$(BUILD_DIR)/bootloader.elf: $(BOOT_SRC) $(HAL_LIB) $(BOOT_STARTUP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DBOOTLOADER $(INC_BOOT) $(LDFLAGS) \
		-Tbootloader/STM32F407VGTx_FLASH_BOOT.ld \
		$(BOOT_SRC) $(HAL_LIB) $(BOOT_STARTUP) -o $@
	$(SIZE) $@

$(BUILD_DIR)/bootloader.bin: $(BUILD_DIR)/bootloader.elf
	$(OBJCOPY) -O binary $< $@

# ---- APP Slot A (ROM 0x08008000) ----
app_slot_a: $(BUILD_DIR)/app_slot_a.bin

$(BUILD_DIR)/app_slot_a.elf: $(APP_SRC) $(HAL_SRC) $(HAL_LIB) $(STARTUP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DAPP_SLOT_A $(INC_APP) $(LDFLAGS) \
		-Tbootloader/STM32F407VGTx_FLASH_SLOTA.ld \
		$(APP_SRC) $(HAL_SRC) $(HAL_LIB) $(STARTUP) -o $@
	$(SIZE) $@

$(BUILD_DIR)/app_slot_a.bin: $(BUILD_DIR)/app_slot_a.elf
	$(OBJCOPY) -O binary $< $@

# ---- APP Slot B (ROM 0x08060000) ----
app_slot_b: $(BUILD_DIR)/app_slot_b.bin

$(BUILD_DIR)/app_slot_b.elf: $(APP_SRC) $(HAL_SRC) $(HAL_LIB) $(STARTUP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DAPP_SLOT_B $(INC_APP) $(LDFLAGS) \
		-Tbootloader/STM32F407VGTx_FLASH_SLOTB.ld \
		$(APP_SRC) $(HAL_SRC) $(HAL_LIB) $(STARTUP) -o $@
	$(SIZE) $@

$(BUILD_DIR)/app_slot_b.bin: $(BUILD_DIR)/app_slot_b.elf
	$(OBJCOPY) -O binary $< $@

# ----
clean:
	rm -rf $(BUILD_DIR)

flash_boot:
	st-flash write $(BUILD_DIR)/bootloader.bin 0x08000000

flash_slot_a:
	st-flash write $(BUILD_DIR)/app_slot_a.bin 0x08008000

flash_slot_b:
	st-flash write $(BUILD_DIR)/app_slot_b.bin 0x08060000

.PHONY: all clean bootloader app_slot_a app_slot_b flash_boot flash_slot_a flash_slot_b
