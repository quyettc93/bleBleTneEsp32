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
int gpioPins[] = {4, 5, 18, 19, 21, 22, 13, 12}; // Các chân GPIO chính
int extendedGpioPins[] = {14, 27, 26};         // Các chân GPIO bổ sung cho byte thứ tư
const int gpioCount = 8;                       // Số lượng GPIO chính được sử dụng
unsigned long gpioTimers[gpioCount];           // Lưu thời gian bắt đầu trạng thái LOW
bool gpioStates[gpioCount] = {false};          // Lưu trạng thái hiện tại của từng GPIO
unsigned long extendedGpioTimers[3];           // Lưu thời gian cho GPIO bổ sung
bool extendedGpioStates[3] = {false};          // Trạng thái GPIO bổ sung
int holdCount = 0;

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
      for (int i = 0; i < 3; i++) {
        pinMode(extendedGpioPins[i], OUTPUT);
        digitalWrite(extendedGpioPins[i], HIGH); // Ban đầu đặt HIGH (24V)
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
        uint8_t processedData[gpioCount * 8] = {0};  // Mảng để lưu trữ dữ liệu binary đã lọc
        int processedIndex = 0;

        // Lấy byte đầu tiên và xử lý như cũ
        for (int bit = 0; bit < 8; bit++) {
            processedData[processedIndex++] = (receivedData[0] & (1 << bit)) ? 1 : 0;
        }

        // Kích hoạt GPIO dựa trên processedData
        for (int i = 0; i < gpioCount; i++) {
            if (processedData[i] == 1 && !gpioStates[i]) {
                Serial.printf("Bit %d active, GPIO %d to 0V\n", i, gpioPins[i]);
                digitalWrite(gpioPins[i], LOW);  // Đưa chân GPIO xuống 0V
                gpioStates[i] = true;            // Đánh dấu trạng thái đang kích hoạt
                gpioTimers[i] = millis();        // Lưu thời gian bắt đầu
            }
        }

        // Lấy byte thứ tư và xử lý 3 chân GPIO bổ sung
for (int bit = 0; bit < 3; bit++) {
        if ((receivedData[3] & (1 << bit)) && (bit != 2)) {
            if (!extendedGpioStates[bit]) {
                Serial.printf("Extra Bit %d active, GPIO %d to 24V\n", bit, extendedGpioPins[bit]);
                digitalWrite(extendedGpioPins[bit], HIGH); // Đưa chân GPIO LÊN 24v
                extendedGpioStates[bit] = true;           // Đánh dấu trạng thái đang kích hoạt
                extendedGpioTimers[bit] = millis();       // Lưu thời gian bắt đầu
            }
        }
         if((receivedData[3] & (1 << bit)) && (bit == 2)) {  // Nếu bit >= 2 (tức bit == 2)
        holdCount++;  // Tăng abc khi bit thứ 3 của byte thứ 4 là 1
        if(holdCount == 10) {  // Nếu abc = 255 thì reset abc về 0
            holdCount = 0;
        }
        Serial.print("holdCount: ");
        Serial.println(holdCount);
        
        // Kiểm tra nếu abc là số lẻ hoặc số chẵn và điều khiển GPIO26
        if (holdCount % 2 == 1) {  // Số lẻ
            digitalWrite(26, HIGH);  // Tắt GPIO26
            Serial.println("GPIO26 is ON (abc is odd).");
        } else {  // Số chẵn
            digitalWrite(26, LOW);  // Bật GPIO26
            Serial.println("GPIO26 is OFF (abc is even).");
        }
    }
    
}
        receivedDataLength = 0;  // Reset lại độ dài dữ liệu sau khi đã xử lý
    }

    // Kiểm tra thời gian và tắt các GPIO chính đã kích hoạt
    for (int i = 0; i < gpioCount; i++) {
        if (gpioStates[i] && (millis() - gpioTimers[i] >= 1000)) {
            Serial.printf("GPIO %d to 24V\n", gpioPins[i]);
            digitalWrite(gpioPins[i], HIGH);  // Đưa chân GPIO xuống 24V
            gpioStates[i] = false;           // Đặt lại trạng thái
        }
    }

    // Kiểm tra thời gian và tắt các GPIO bổ sung đã kích hoạt
    for (int i = 0; i < 3; i++) {
        if (extendedGpioStates[i] && (millis() - extendedGpioTimers[i] >= 1000)) {
            Serial.printf("Extended GPIO %d to 0V\n", extendedGpioPins[i]);
            digitalWrite(extendedGpioPins[i], LOW);  // Đưa chân GPIO lên 0v
            extendedGpioStates[i] = false;           // Đặt lại trạng thái
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