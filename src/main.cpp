#include <Arduino.h> //khai báo thư viện
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;

// Biến lưu trữ dữ liệu nhận từ client
uint8_t receivedData[8];
int receivedDataLength = 0;  // Để theo dõi độ dài dữ liệu nhận

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"


// Chân GPIO để điều khiển LED D2
#define LED_PIN 2
int gpioPins[] = {4, 5, 18, 19, 21, 22, 13, 12}; // Các chân GPIO mới
const int gpioCount = 8;                 // Số lượng GPIO được sử dụng
unsigned long gpioTimers[gpioCount];     // Lưu thời gian bắt đầu trạng thái LOW
bool gpioStates[gpioCount] = {false};    // Lưu trạng thái hiện tại của từng GPIO

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

  // Cấu hình LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Đảm bảo LED tắt khi bắt đầu

    // Cấu hình các chân GPIO mới làm OUTPUT
  for (int i = 0; i < 8; i++) {
    pinMode(gpioPins[i], OUTPUT);
    digitalWrite(gpioPins[i], HIGH); // Mặc định là 24V (HIGH)
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
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE
  );

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
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}

void loop() {
 // Kiểm tra nếu có dữ liệu nhận được và xử lý
    if (receivedDataLength > 0) {
        Serial.print("Received Data in Loop: ");
        uint8_t processedData[receivedDataLength * 8] = {0};  // Mảng để lưu trữ dữ liệu binary đã lọc
        int processedIndex = 0;

        // Chuyển đổi dữ liệu nhận được thành binary và lưu vào processedData
        for (int i = 0; i < receivedDataLength; i++) {
            Serial.print(receivedData[i], BIN);  // Hiển thị từng byte theo dạng BIN
            Serial.print(" ");

            // Lưu từng bit của byte vào mảng processedData
            for (int bit = 0; bit < 8; bit++) {
                processedData[processedIndex++] = (receivedData[i] & (1 << bit)) ? 1 : 0;
            }
        }
        Serial.println();

        // Kích hoạt GPIO dựa trên processedData
        for (int i = 0; i < processedIndex; i++) {
            if (processedData[i] == 1) {
                int gpioIndex = i % gpioCount;  // Lấy chỉ số GPIO phù hợp với bit hiện tại
                if (!gpioStates[gpioIndex]) {  // Nếu GPIO chưa được kích hoạt
                    Serial.printf("Bit %d active, GPIO %d to 0V\n", gpioIndex, gpioPins[gpioIndex]);
                    digitalWrite(gpioPins[gpioIndex], LOW);  // Đưa chân GPIO xuống 0V
                    gpioStates[gpioIndex] = true;           // Đánh dấu trạng thái đang kích hoạt
                    gpioTimers[gpioIndex] = millis();       // Lưu thời gian bắt đầu
                }
            }
        }

        receivedDataLength = 0;  // Reset lại độ dài dữ liệu sau khi đã xử lý
    }

    // Kiểm tra thời gian và tắt các GPIO đã kích hoạt
    for (int i = 0; i < gpioCount; i++) {
        if (gpioStates[i] && (millis() - gpioTimers[i] >= 1000)) {
            Serial.printf("GPIO %d to 24V\n", gpioPins[i]);
            digitalWrite(gpioPins[i], HIGH);  // Đưa chân GPIO lên 24V
            gpioStates[i] = false;           // Đặt lại trạng thái
        }
    }

  // Quản lý kết nối và ngắt kết nối
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);  // Cho stack Bluetooth cơ hội khởi động lại
    pServer->startAdvertising();  // Restart quảng bá BLE
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }

  // Kết nối lại khi có thiết bị mới kết nối
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // Nhấp nháy LED khi có thiết bị kết nối
  if (deviceConnected) {
    digitalWrite(LED_PIN, (millis() / 500) % 2);  // Nhấp nháy LED mỗi 500ms
  }
}