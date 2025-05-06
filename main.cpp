/**
  CYD_BLE_ENERGY_RECORDER
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

*/
//
#include <Arduino.h>
#include <string.h>
#include "FS.h"
#include "SD.h"
#include "BLEDevice.h"
#include <Adafruit_GFX.h>     // Core graphics library
#include <TFT_eSPI.h>         // User_Setup.h replaced by Rui Santos' version (https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/)
//
// define SD Card SPI-Pins
#define SDC_MOSI  23
#define SDC_MISO  19
#define SDC_CLK   18
#define SDC_CS    5
//
SPIClass sdc_spi = SPIClass(VSPI);
String logFile = "/PowerMeterLog.txt";
bool sdOK = false;
//
TFT_eSPI tft = TFT_eSPI();
//
// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID("0000FFE1-0000-1000-8000-00805F9B34FB");
//
static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myDevice;
//
// Energy data
struct vipe {
	double volts;		  // voltage
	double amps;		  // current
	double watts;		  // power
	double pf;			  // power factor
	double kwh=0.0;	  // energy (cummulative)
	double hz;			  // frequency
};
vipe energy;
//
uint32_t startTime;
String errMsg_SDC = "No SD card. Data not logged!",
       errMsg_BLE = "No BLE connection. No data to display!";
#define ERR_MSG_Y 28  // vertical position of the error message

//////////////

void writeFile(fs::FS &fs, const char * path, const char * message) {
	// Write to the SD card (DON'T MODIFY THIS FUNCTION)
  //
	Serial.printf("Writing file: %s\n", path);

	File file = fs.open(path, FILE_WRITE);
	if (!file) {
		Serial.println("Failed to open file for writing");
		return;
	}
	if (file.print(message)) {
		Serial.println("File written");
	} else {
		Serial.println("Write failed");
	}
	file.close();
}

//////////////

void createFile(fs::FS &fs, const char * path, const char * labels) {
	// If the log file doesn't exist, create the file on the SD card
	// and write the data labels as its first record
	//
	Serial.printf("Attemptng to open file %s\r\n",path);
	File file = SD.open(path);
	if (!file) {
		Serial.println("File doens't exist");
		Serial.println("Creating file...");
		writeFile(SD, path, labels);
	}
	else {
		Serial.printf("File %s already exists. Appending.\r\n",path);  
	}
	//
	file.close();
}

//////////////

void appendFile(fs::FS &fs, const char * path, const char * message) {
	// Append data to the SD card (DON'T MODIFY THIS FUNCTION)
	//
	Serial.printf("Appending to file: %s\n", path);
	File file = fs.open(path, FILE_APPEND);
	if (!file) {
		Serial.println("Failed to open file for appending");
		return;
	}
	if (file.print(message)) {
		Serial.println("Message appended");
	} else {
		Serial.println("Append failed");
	}
	file.close();
}

//////////////

static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
	uint32_t tmp;
  //
  Serial.print("Notify callback for characteristic ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" of data length ");
  Serial.println(length);
  Serial.print("data: ");
	for (int i = 0; i<length; i++) {
	  if (pData[i] < 0x10) {Serial.print("0");}
	  Serial.print(pData[i], HEX);
	}
  Serial.println();
	//
	// Now, we need to extract the numerical values from pData
	tmp = ((uint32_t)pData[4] << 16) |
		  ((uint32_t)pData[5] << 8)  |
		  ((uint32_t)pData[6]);
	energy.volts = 0.1*tmp;
  Serial.printf("Voltage:   %6.1f V\n",energy.volts);
	//
	tmp = ((uint32_t)pData[7] << 16) |
		  ((uint32_t)pData[8] << 8)  |
		  ((uint32_t)pData[9]);
	energy.amps = 0.001*tmp;
  Serial.printf("Current:   %6.3f A\n",energy.amps);
	//
	tmp = ((uint32_t)pData[10] << 16) |
		  ((uint32_t)pData[11] << 8)  |
		  ((uint32_t)pData[12]);
	energy.watts = 0.1*tmp;
  Serial.printf("Power:     %6.1f W\n",energy.watts);
	//
	/*tmp = ((uint32_t)pData[13] << 24) |
		  ((uint32_t)pData[14] << 16) |
		  ((uint32_t)pData[15] << 8)  |
		  ((uint32_t)pData[16]);
	energy.kwh = 0.01*tmp;*/
  // The Atorch S1B socket calculates the cumulative energy in kWh.
  // The accumulation starts after system reset.
  // This Energy Recorder begins the accumulation of energy value every time
  // the program starts. Therefore, instead of using the energy sent in a BLE message,
  // every second we calculate the energy increment (dP = voltage*current [Ws]) 
  // and convert it to kWh (dividing by 3600000).
  energy.kwh = energy.kwh + energy.volts*energy.amps/3600000;
  Serial.printf("Energy:  %8.5f kWh\n",energy.kwh);
	//
	tmp = ((uint32_t)pData[20] << 8) |
		  ((uint32_t)pData[21]);
	energy.hz = 0.1*tmp;
  Serial.printf("Frequency:   %4.1f Hz\n",energy.hz);
	//
	tmp = ((uint32_t)pData[22] << 8) |
		  ((uint32_t)pData[23]);
	energy.pf = 0.001*tmp;
  Serial.printf("Power Factor: %4.2f\n",energy.pf);
};

//////////////

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {}

  void onDisconnect(BLEClient *pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

//////////////

bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remote BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");
  pClient->setMTU(517);  // set client to request maximum MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");
  //
  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead()) {
    std::string value = pRemoteCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());
  }
  //
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
  }
  //
  connected = true;
  return true;
}

//////////////

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  /**
  * Scan for BLE servers and find the first one that advertises the service we are looking for.
  * 
  * Called for each advertising BLE server.
  */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;
    } // Found our server
  } // onResult
};// MyAdvertisedDeviceCallbacks

//////////////

String hms0(int x) {
	// Returns a 2-digit number with a leading zero if x < 10
	//
	String s = String(x);
	if (x < 10) s = "0" + s;
	//
	return s;
};

//////////////

String secs2hhmmss(uint32_t secs){
  // Convert the running time from seconds to a string "hh:mm:ss"
  //
  int hh,mm,ss;
  //
  hh = secs/3600;
  mm = (secs - hh*3600)/60;
  ss = secs - hh*3600 - mm*60;
  return (hms0(hh) + ":" + hms0(mm) + ":" + hms0(ss));
};

//////////////

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  //
  // Initialize TFT
  tft.init();
  tft.setRotation(1);   // 1 and 3 are landscape
  //
  // Clear the display
  tft.fillScreen(TFT_BLACK);
  //
  // Display the program name and version
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("ENERGY RECORDER  v1.0", 0, 0, 4);
	//
	// Mount the SD card
	sdc_spi.begin(SDC_CLK, SDC_MISO, SDC_MOSI, SDC_CS);
	if (!SD.begin(SDC_CS,sdc_spi,4000000,"/sd",5,true)){
		Serial.println("SD Card Mount Failed");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(errMsg_SDC, 0, ERR_MSG_Y, 2);
		sdOK = false;
	} else {
		Serial.println("SD Card Mounted");
		writeFile(SD, logFile.c_str(), "Time [s], Voltage [V], Current [A], Power [W], Power Factor, Energy [kWh], Frequency [Hz]\r\n");
		sdOK = true;
	}
  tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
  //
  // Initiate a BLE connection
  BLEDevice::init("");
  //
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device. Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  //
  // Record the start time
  startTime = millis();
}  // End of setup.

//////////////

void loop() {
  //
  uint32_t newSecs;
  char buffer[16];
  String hms, newMillis, newValue, vol, cur, pow, ene, pfa, fre;
  //
  // Perform the calculations every second
  uint32_t currentTime = millis();
  if (currentTime > startTime + 999) {
    startTime = currentTime;
    //
    // If the flag "doConnect" is true then we have scanned for and found the desired
    // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
    // connected we set the connected flag to be true.
    if (doConnect == true) {
      if (connectToServer()) {
        Serial.println("We are now connected to the BLE Server.");
      } else {
        Serial.println("We have failed to connect to the server; there is nothing more we will do.");
      }
      doConnect = false;
    }
    //
    // If we are connected to a peer BLE Server, update the characteristic each time we are reached
    // with the current time since boot.
    if (connected) {
      newSecs = startTime/1000;
      newValue = "Time since boot: " + String(newSecs);
      Serial.println("Setting new characteristic value to \"" + newValue + "\"");

      // Set the characteristic's value to be the array of bytes that is actually a string.
      pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
    } else if (doScan) {
      BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
    }
    //
    // Only if we are connected
    if (connected) {
      //
      // Display the energy data
      vol = String(energy.volts,1);
      cur = String(energy.amps,3);
      pow = String(energy.watts,1);
      ene = String(energy.kwh,5);
      pfa = String(energy.pf,2);
      fre = String(energy.hz,1);
      hms = secs2hhmmss(newSecs);
      //
      //
      tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
      tft.drawString("Run Time:", 0, 50, 4); tft.drawString(hms, 120, 50, 4);
			tft.drawString("Voltage:", 0, 76, 4); tft.drawString(vol + " V", 120, 76, 4);
      dtostrf(energy.amps, 6, 3, buffer);   // dtostrf() is used to format a floating point number
			tft.drawString("Current:", 0, 102, 4); tft.drawString(String(buffer) + " A  ", 120, 102, 4);
      dtostrf(energy.watts, 6, 1, buffer);
			tft.drawString("Power:", 0, 128, 4); tft.drawString(String(buffer) + " W   ", 120, 128, 4);
			tft.drawString("PF:", 0, 154, 4); tft.drawString(pfa, 120, 154, 4);
      dtostrf(energy.kwh, 8, 5, buffer);
			tft.drawString("Energy:", 0, 180, 4); tft.drawString(String(buffer) + " kWh   ", 120, 180,4);
			tft.drawString("Frequency: " + fre + " Hz", 0, 206, 4);
      //
      if (sdOK) {
        //
        // Write the energy data to a log file
        newValue = hms + "," + vol + "," + cur + "," + pow + "," + pfa+ "," + ene  + "," + fre + "\r\n";
        appendFile(SD, logFile.c_str(), newValue.c_str());         
        Serial.print(newValue);
      } else {
      //
      // Display the SD card error message
      tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString(errMsg_SDC, 0, ERR_MSG_Y, 2);
      }
    } else {
      //
      // Display the BLE error message
      Serial.println(errMsg_BLE);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString(errMsg_BLE, 0, ERR_MSG_Y, 2);
    }
  }
  //
};// End of loop
 