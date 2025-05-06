# CYD_BLE_ENERGY_RECORDER
  Written by Ben Rodanski

  This program periodically reads and records (on a micro SD card) the electric power consumption of an appliance, 
  connected to an S1B smart programmable socket by Atorch (https://www.aliexpress.com/item/1005004588043273.html).
  Communication with the S1B socket is via Bluetooth Low Energy (BLE) protocol.
  Every second the program records the instantaneous voltage [V], current [A], power [W], power factor, energy [kWh], 
  and frequency [Hz].

  The energy data is extracted from a 36-byte message sent by the S1B socket.
  A sample message looks like this:

  FF5501010009BC0000990001240000001100006401F402FD002300000A0D3C00000000C1

  According to GitHub (https://github.com/syssi/esphome-atorch-dl24/blob/main/docs/protocol-design.md)
  the message structure is (the byte values, given below, are taken from the sample message;
  byte numbering starts from 00):
  
    bytes 00-01, FF 55        - magic header
    byte  02,    01           - message type (01=Report)
    byte  03,    01           - device type (01=AC meter)
    bytes 04-06, 00 09 BC     - voltage [V*10]
    bytes 07-09, 00 00 99     - current [mA*10]
    bytes 10-12, 00 01 24     - power [W*10]
    bytes 13-16, 00 00 00 11  - energy [kWh*100]
    bytes 17-19, 00 00 64     - price [c/kWh]
    bytes 20-21, 01 F4        - frequency [Hz]
    bytes 22-23, 02 FD        - power factor*1000
    bytes 24-25, 00 23        - temperature
    bytes 26-27, 00 00        - hour
    byte  28,    0A           - minute
    byte  29,    0D           - second
    byte  30,    3C           - backlight time (seconds)
    byte  31-34, 00 00 00 00  - unspecified
    byte  35,    C1           - checksum
  
  The program was developed using MS Visual Studio Code IDE with PlatformIO (Core 6.1.18) and tested on
  a Cheap Yellow Display (CYD) ESP32 board (https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/).

  Version.Revision Status
  2025-05-07  v1.0  First stable release
