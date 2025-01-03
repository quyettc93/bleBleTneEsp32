#include <Arduino.h> //khai báo thư viện
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_task_wdt.h>

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;

// Biến lưu trữ dữ liệu nhận từ client
uint8_t receivedData[8];
int receivedDataLength = 0; // Để theo dõi độ dài dữ liệu nhận

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Chân GPIO để điều khiển LED D2
#define LED_PIN 2
#define LED_PIN_WATCHDOG 32                      // Chân GPIO Watchdog
int gpioPins[] = {4, 5, 18, 19, 21, 22, 13, 12}; // Các chân GPIO chính
int extendedGpioPins[] = {14, 27, 26}; // Các chân GPIO bổ sung cho byte thứ tư
const int gpioCount = 8; // Số lượng GPIO chính được sử dụng
unsigned long gpioTimers[gpioCount]; // Lưu thời gian bắt đầu trạng thái LOW
bool gpioStates[gpioCount] = {false}; // Lưu trạng thái hiện tại của từng GPIO
unsigned long extendedGpioTimers[3];  // Lưu thời gian cho GPIO bổ sung
bool extendedGpioStates[3] = {false}; // Trạng thái GPIO bổ sung
bool holdCount = false;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    BLEDevice::startAdvertising();
    // Bật LED khi có thiết bị kết nối
    digitalWrite(LED_PIN, HIGH);
  };

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    // Tắt LED khi không còn thiết bị kết nối
    digitalWrite(LED_PIN, LOW);
  }
};

// Định nghĩa một callback mới để xử lý dữ liệu ghi từ client
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    // Nhận giá trị đã được ghi từ client
    std::string received = pCharacteristic->getValue();

    // Lưu trữ dữ liệu nhận được vào mảng receivedData
    receivedDataLength = received.length();
    for (int i = 0; i < receivedDataLength; i++) {
      receivedData[i] = received[i];
    }
  }
};

void setup() {
  Serial.begin(115200);
  esp_task_wdt_init(10,
                    true); // Timeout 10 giây, reset hệ thống khi quá thời gian
  esp_task_wdt_add(NULL); // Thêm task chính vào WDT

  // Cấu hình LED Watchdog
  pinMode(LED_PIN_WATCHDOG, OUTPUT);   // Đặt chân GPIO là OUTPUT
  digitalWrite(LED_PIN_WATCHDOG, LOW); // Đảm bảo LED tắt khi bắt đầu

  // Cấu hình LED D2 báo hiệu kết nối
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Đảm bảo LED tắt khi bắt đầu

  // Cấu hình các chân GPIO mới làm OUTPUT
  for (int i = 0; i < 8; i++) {
    pinMode(gpioPins[i], OUTPUT);
    digitalWrite(gpioPins[i], LOW); // Mặc định là 0V (LOW)
  }
  for (int i = 0; i < 3; i++) {
    pinMode(extendedGpioPins[i], OUTPUT);
    digitalWrite(extendedGpioPins[i], LOW); // Ban đầu đặt thấp (0V)
  }

  // Create the BLE Device
  BLEDevice::init("ESP32 QUYET");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_WRITE |
                               BLECharacteristic::PROPERTY_NOTIFY |
                               BLECharacteristic::PROPERTY_INDICATE);

  // Gắn callback cho characteristic để nhận dữ liệu từ client
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(
      0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}
// THỬ NGHIỆM
void loop() {
  // Reset Watchdog để tránh reset hệ thống
  esp_task_wdt_reset();
  // Nhấp nháy LED để kiểm tra hệ thống hoạt động (Watchdog)
  digitalWrite(LED_PIN_WATCHDOG,
               (millis() / 500) % 2); // Nhấp nháy LED mỗi 500ms

  // Kiểm tra nếu có dữ liệu nhận được và xử lý
  if (receivedDataLength > 0) {
    Serial.print("Received Data in Loop: ");

    // Lấy byte thứ nhất và xử lý 8 chân GPIO chính
    for (int i = 0; i < gpioCount; i++) {
      if (receivedData[0] & (1 << i)) {
        Serial.printf("Bit %d active, GPIO %d to 0V\n", i, gpioPins[i]);
        digitalWrite(gpioPins[i], HIGH); // Đưa chân GPIO lên 24v
        gpioStates[i] = true; // Đánh dấu trạng thái đang kích hoạt
        gpioTimers[i] = millis(); // Lưu thời gian bắt đầu
      }
    }

    // Lấy byte thứ tư và xử lý 3 chân GPIO bổ sung
    for (int bit = 0; bit < 3; bit++) {
      if ((receivedData[3] & (1 << bit)) && (bit != 2)) {
        if (!extendedGpioStates[bit]) {
          if (bit == 1 && holdCount) {
            return;
          }
          Serial.printf("Extra Bit %d active, GPIO %d to 24V\n", bit,
                        extendedGpioPins[bit]);
          digitalWrite(extendedGpioPins[bit], HIGH); // Đưa chân GPIO LÊN 24v
          extendedGpioStates[bit] = true; // Đánh dấu trạng thái đang kích hoạt
          extendedGpioTimers[bit] = millis(); // Lưu thời gian bắt đầu
        }
      }
      if ((receivedData[3] & (1 << bit)) &&
          (bit == 2)) {         // Nếu bit >= 2 (tức bit == 2)
        holdCount = !holdCount; // Chuyển trạng thái holdCount
        Serial.print("holdCount: ");
        Serial.println(holdCount);

        // Kiểm tra nếu holdCount là true hoặc false để bật hoặc tắt GPIO26
        if (holdCount) {          // true
          digitalWrite(27, HIGH); // Tắt GPIO26
          Serial.println("GPIO26 is ON (holdCount is odd).");
        } else {                 // false
          digitalWrite(27, LOW); // Bật GPIO26
          Serial.println("GPIO26 is OFF (holdCount is even).");
        }
      }
    }
    receivedDataLength = 0; // Reset lại độ dài dữ liệu sau khi đã xử lý
  }
  // Kiểm tra thời gian và tắt các GPIO chính đã kích hoạt
  for (int i = 0; i < gpioCount; i++) {
    if (gpioStates[i] && (millis() - gpioTimers[i] >= 1000)) {
      Serial.printf("GPIO %d to 24V\n", gpioPins[i]);
      digitalWrite(gpioPins[i], LOW); // Đưa chân GPIO xuống 24V
      gpioStates[i] = false;          // Đặt lại trạng thái
    }
  }
  // Kiểm tra thời gian và tắt các GPIO bổ sung đã kích hoạt
  for (int i = 0; i < 3; i++) {
    if (extendedGpioStates[i] && (millis() - extendedGpioTimers[i] >= 1000)) {
      Serial.printf("Extended GPIO %d to 0V\n", extendedGpioPins[i]);
      digitalWrite(extendedGpioPins[i], LOW); // Đưa chân GPIO lên 0v
      extendedGpioStates[i] = false;          // Đặt lại trạng thái
    }
  }

  // Quản lý kết nối và ngắt kết nối
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // Cho stack Bluetooth cơ hội khởi động lại
    pServer->startAdvertising(); // Restart quảng bá BLE
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }

  // Kết nối lại khi có thiết bị mới kết nối
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // Nhấp nháy LED khi có thiết bị kết nối
  if (deviceConnected) {
    digitalWrite(LED_PIN, (millis() / 1000) % 2); // Nhấp nháy LED mỗi 500ms
  }
}