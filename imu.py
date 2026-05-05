import asyncio
import csv
import logging
import struct
import threading
import datetime
from typing import List
from bleak import BleakScanner, BleakClient

# ── Configuration ────────────────────────────────────────────────────────────
DEVICE_NAME = "XIAO_IMU111"
CHARACTERISTIC_UUID = "12345678-1234-5678-1234-56789abcde01"
OUTPUT_FILE = f"imu_data_{datetime.datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}.csv"
FRAME_LENGTH = 120 * 6  # Number of samples per frame

# Set up logging for cleaner output
logging.basicConfig(level=logging.INFO, format='%(message)s')
logger = logging.getLogger(__name__)

class IMUDataCollector:
    def __init__(self, filename: str):
        self.filename = filename
        self.csv_file = open(filename, mode="w", newline="")
        self.csv_writer = csv.writer(self.csv_file)
        
        # State management
        self.frame_buffer: List[float] = [0.0] * FRAME_LENGTH
        self.frame_index = 0
        self.frame_type = 0
        self.is_connected = False
        self._lock = threading.Lock()

    def handle_data(self, _: int, data: bytearray):
        """Processes incoming bytes and saves to buffer."""
        if self.frame_type == 0:
            return

        try:
            # Unpack 6 signed 16-bit integers
            vals = struct.unpack("<hhhhhh", data[:12])
            
            # Apply scaling: Accel/1000, Gyro/100
            processed = [vals[i] / 1000.0 if i < 3 else vals[i] / 100.0 for i in range(6)]

            with self._lock:
                # Add to buffer
                start = self.frame_index
                self.frame_buffer[start:start+6] = processed
                self.frame_index += 6

                # Check if buffer is full
                if self.frame_index >= FRAME_LENGTH:
                    self._save_frame()

        except struct.error:
            logger.warning("⚠️ Invalid BLE Data Packet received.")

    def _save_frame(self):
        """Internal helper to write the buffer to CSV."""
        row = [self.frame_type] + self.frame_buffer
        self.csv_writer.writerow(row)
        self.csv_file.flush()
        
        # Reset state
        self.frame_index = 0
        self.frame_type = 0
        logger.info("💾 Saved a new IMU frame!")

    def set_frame_type(self, value: int):
        with self._lock:
            self.frame_type = value

    def close(self):
        self.csv_file.close()

# ── BLE Operations ───────────────────────────────────────────────────────────
async def run_ble_client(collector: IMUDataCollector):
    logger.info(f"🔍 Scanning for '{DEVICE_NAME}'...")
    
    device = await BleakScanner.find_device_by_filter(
        lambda d, _: d.name == DEVICE_NAME, timeout=10.0
    )
    
    if not device:
        logger.error("❌ Device not found")
        return

    async with BleakClient(device.address, timeout=20.0) as client:
        collector.is_connected = True
        logger.info(f"✅ Connected to {device.address}")
        
        await client.start_notify(CHARACTERISTIC_UUID, collector.handle_data)
        
        # Keep loop running
        while True:
            await asyncio.sleep(1)

def start_background_loop(loop: asyncio.AbstractEventLoop, collector: IMUDataCollector):
    asyncio.set_event_loop(loop)
    loop.run_until_complete(run_ble_client(collector))

# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    collector = IMUDataCollector(OUTPUT_FILE)
    
    # Start BLE in a background thread
    new_loop = asyncio.new_event_loop()
    ble_thread = threading.Thread(
        target=start_background_loop, 
        args=(new_loop, collector), 
        daemon=True
    )
    ble_thread.start()

    print("Main app running. Waiting for connection...")
    
    try:
        while True:
            # Simple non-blocking input handling
            if collector.is_connected and collector.frame_type == 0:
                user_input = input("Enter Frame Type (or 'q' to quit): ")
                if user_input.lower() == 'q':
                    break
                collector.set_frame_type(int(user_input))
            
            # Allow the main thread to handle other tasks or stay idle
            import time
            time.sleep(0.1) 
            
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        collector.close()

if __name__ == "__main__":
    main()