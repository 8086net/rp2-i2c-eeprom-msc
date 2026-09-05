# USB CDC MSC (Mass Storage Class) to 24C32 I2C EEPROM bridge

`rp2-i2c-eeprom` presents an I2C EEPROM (24C32) connected to GP0/GP1 of the Raspberry Pi Pico/Pico2 or Waveshare Core2350B as a USB CDC MSC (Mass Storace Class) devicei to allow simple reading and writing of the EEPROM.

The 24C32 stores 4096 bytes which is presented as eight 512 byte blocks, these blocks are written directly to the EEPROM so for normal use you would write to the device directly /dev/sdX rather than to a partition (/dev/sdX1 for example).


