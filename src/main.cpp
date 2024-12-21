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
int gpioPins[] = {4, 5, 18, 19, 21, 22, 23, 13}; // Các chân GPIO mới

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
    // Kiểm tra nếu có dữ liệu nhận được và in nó ra
  if (receivedDataLength > 0) {
    Serial.print("Received Data in Loop: ");
    for (int i = 0; i < receivedDataLength; i++) {
      Serial.print(receivedData[i], BIN);  // Hiển thị từng byte theo dạng BIN
      Serial.print(" ");

      // Áp dụng giá trị binary vào các chân GPIO đã chỉ định
      for (int bit = 0; bit < 8; bit++) {
        // Kiểm tra từng bit trong byte
        if (receivedData[i] & (1 << bit)) {
          Serial.printf("Bit %d active, GPIO %d to 0V for 1s\n", bit, gpioPins[bit]);
          digitalWrite(gpioPins[bit], LOW); // Đưa chân GPIO xuống 0V
          delay(1000);               // Giữ trạng thái 1 giây
          digitalWrite(gpioPins[bit], HIGH); // Đưa chân GPIO lên 24V
        }
      }
    }

    Serial.println();
    receivedDataLength = 0;  // Reset lại độ dài dữ liệu sau khi đã in ra
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