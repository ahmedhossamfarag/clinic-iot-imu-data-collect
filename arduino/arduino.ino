/*
 * 📝 BLE IMU Broadcaster
 * ----------------------
 * Streams accelerometer + gyroscope data from the LSM6DS3 sensor
 * as BLE notifications.
 * * 📱 Test with:
 * - nRF Connect app (subscribe to notifications)
 * - Raspberry Pi Python script (visualizes motion)
 *
 * =================================================================
 * HOW THE IMU WORKS:
 * An IMU (Inertial Measurement Unit) tracks movement. 
 * - Accelerometer: Measures linear acceleration (G-forces) along X, Y, Z axes.
 * - Gyroscope: Measures rotational speed (degrees per second) around X, Y, Z axes.
 * =================================================================
 */


#define DELAY 20
#define DEVICE_NAME "XIAO_IMU111"
#define IMU_SERVICE_UUID "12345678-1234-5678-1234-56789abcde00"
#define IMU_DATA_CHAR_UUID "12345678-1234-5678-1234-56789abcde01"

#include <ArduinoBLE.h>   // Official Arduino library for BLE communication
#include "Wire.h"         // Standard library for I2C communication (how the board talks to the sensor)
#include "LSM6DS3.h"      // Specific driver library for the LSM6DS3 IMU chip

// 🆕 Create an instance of the LSM6DS3 IMU
// We tell it to use I2C_MODE. 0x6A is the standard hardware I2C address for this specific chip.
LSM6DS3 imu(I2C_MODE, 0x6A); 

// Define BLE Service and Characteristic UUIDs
// We are using 128-bit custom UUIDs here instead of the short 16-bit standard ones.
BLEService imuService(IMU_SERVICE_UUID);

// IMPORTANT BANDWIDTH OPTIMIZATION:
// A standard float is 4 bytes. Sending 6 floats (3 accel + 3 gyro) = 24 bytes.
// Instead, we will compress them into 16-bit integers (2 bytes each). 6 * 2 = 12 bytes.
// This saves BLE bandwidth and makes transmission faster and more reliable.
BLECharacteristic imuDataChar(
  IMU_DATA_CHAR_UUID,
  BLERead | BLENotify, // Allows Python to read on demand, OR subscribe to live pushed updates
  12 // Explicitly telling the BLE stack this characteristic holds exactly 12 bytes
);

void setup() {
  Serial.begin(9600);
  while (!Serial);

  // Initialize BLE Hardware
  if (!BLE.begin()) {
    Serial.println("❌ BLE Initialization Failed!");
    while (1);
  }

  // Initialize IMU Hardware
  // imu.begin() configures the chip. If it returns anything other than 0, it failed to boot.
  if (imu.begin() != 0) {
    Serial.println("❌ IMU Initialization Failed!");
    while (1);
  }

  // Build the BLE Profile: Attach characteristic -> service -> BLE stack
  imuService.addCharacteristic(imuDataChar);
  BLE.addService(imuService);

  // Set advertised name (what Python searches for) and attach our service UUID to the broadcast
  BLE.setLocalName(DEVICE_NAME);
  BLE.setAdvertisedService(imuService);

  // Turn on the radio
  BLE.advertise();
  Serial.println("📡 BLE IMU Broadcaster Ready");
}

void loop() {
  // Check if a central device (like your Python script) is trying to connect
  BLEDevice central = BLE.central();

  if (central) {
    Serial.print("🔗 Connected to: ");
    Serial.println(central.address());

    // Stream IMU data continuously AS LONG AS the connection remains active
    while (central.connected()) {
      
      // Read raw floating point values from the sensor
      float ax = imu.readFloatAccelX();
      float ay = imu.readFloatAccelY();
      float az = imu.readFloatAccelZ();
      float gx = imu.readFloatGyroX();
      float gy = imu.readFloatGyroY();
      float gz = imu.readFloatGyroZ();

      // THE COMPRESSION TRICK:
      // We multiply the floats by a scaling factor to keep their decimal precision, 
      // then chop off the remaining decimals by casting them to int16_t (whole numbers).
      // Example: ax = 1.052 Gs -> 1.052 * 1000 = 1052. 
      // The Python script will divide by 1000 later to get 1.052 back.
      int16_t data[6] = {
        int16_t(ax * 1000), int16_t(ay * 1000), int16_t(az * 1000),
        int16_t(gx * 100),  int16_t(gy * 100),  int16_t(gz * 100)
      };

      // Cast our integer array into a raw byte array (uint8_t*) and push it over BLE
      imuDataChar.setValue((uint8_t*)data, sizeof(data));

      // Debug print to the local Arduino Serial Monitor
      // Serial.print("Accel: "); Serial.print(ax, 3); Serial.print(", ");
      // Serial.print(ay, 3); Serial.print(", "); Serial.println(az, 3);
      // Serial.print("Gyro:  "); Serial.print(gx, 2); Serial.print(", ");
      // Serial.print(gy, 2); Serial.print(", "); Serial.println(gz, 2);
      // Serial.println();

      // Delay to send data at roughly 10Hz (10 times a second).
      // Note: delay() pauses the processor, but it is fine here since sending data is its only job.
      delay(DELAY); 
    }

    // This code only runs when the 'while(central.connected())' loop breaks (device disconnects)
    Serial.print("❌ Disconnected from: ");
    Serial.println(central.address());
  }
}