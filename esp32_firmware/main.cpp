#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ==========================================
// 1. 蓝牙 BLE 配置 (UUID 可自行修改)
// ==========================================
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_RX "beb5483e-36e1-4688-b7f5-ea07361b26a8" // 接收小程序数据
#define CHARACTERISTIC_UUID_TX "1cce2a0d-df8d-4fbc-bd98-75c3db739199" // 向小程序发送状态

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// ==========================================
// 2. 串口配置 (连接沙发 MCU)
// ESP32-C3 建议引脚: TX=21, RX=20 (可根据实际接线修改)
// ==========================================
#define MCU_RX_PIN 20 // ESP32的RX，接沙发的TX
#define MCU_TX_PIN 21 // ESP32的TX，接沙发的RX
#define MCU_BAUD 19200

// ==========================================
// 3. 定时器与状态机配置
// ==========================================
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 50000; // 50秒发一次心跳
uint8_t packetSN = 0; // 包序号，0-255循环

// ==========================================
// 4. 辅助函数：计算校验和并发送数据到 MCU
// ==========================================
void sendToMCU(uint8_t cmd, uint8_t action, uint8_t* payload, size_t payloadLen) {
    // 1. 计算总长度: len(命令字1 + 序号1 + flags2 + action1 + payloadLen + 校验和1)
    uint16_t len = 1 + 1 + 2 + (action != 0 ? 1 : 0) + payloadLen + 1;
    
    // 2. 构造基础数据缓存 (不包含 FF FF 包头)
    uint8_t buffer[64];
    size_t idx = 0;
    
    buffer[idx++] = (len >> 8) & 0xFF; // 长度高字节
    buffer[idx++] = len & 0xFF;        // 长度低字节
    
    int chksumStartIdx = idx; // 从命令字开始算校验和
    buffer[idx++] = cmd;
    buffer[idx++] = packetSN++;
    buffer[idx++] = 0x00; // Flags High
    buffer[idx++] = 0x00; // Flags Low
    
    if (action != 0) {
        buffer[idx++] = action;
    }
    
    for (size_t i = 0; i < payloadLen; i++) {
        buffer[idx++] = payload[i];
    }
    
    // 3. 计算校验和
    uint16_t sum = 0;
    for (size_t i = chksumStartIdx; i < idx; i++) {
        sum += buffer[i];
    }
    uint8_t checksum = sum % 256;
    buffer[idx++] = checksum;
    
    // 4. 串口实际发送 (处理 FF 55 转义)
    Serial1.write(0xFF); // 包头1
    Serial1.write(0xFF); // 包头2
    
    for (size_t i = 0; i < idx; i++) {
        Serial1.write(buffer[i]);
        if (buffer[i] == 0xFF) {
            Serial1.write(0x55); // 非包头部分遇到 FF 必须补 55
        }
    }
    
    Serial.printf("Sent to MCU, Cmd: 0x%02X, SN: %d\n", cmd, packetSN - 1);
}

// ==========================================
// 5. 核心控制函数：将小程序的简易指令翻译为机智云长包
// ==========================================
void handleControlCommand(uint8_t controlCode) {
    // 根据抓包数据，控制包结构为：
    // [0x11 (Action)] + [7字节 attr_flags] + [变长 attr_vals]
    // 这里我们假设小程序发来的 controlCode 代表某种模式或按键
    // 您可以根据实际需要扩展这里的 switch-case
    
    uint8_t payload[16] = {0};
    size_t payloadLen = 0;
    
    // 这是一个通用控制模板：改变某个具有特定 Flag 的状态值
    // 示例：开启按摩 (参考抓包: FF FF 00 0E 03 06 00 00 11 00 00 00 00 20 00 00 01 49)
    if (controlCode == 0x01) { 
        // 7字节 Flags: 00 00 00 00 20 00 00
        payload[0] = 0x00; payload[1] = 0x00; payload[2] = 0x00; payload[3] = 0x00;
        payload[4] = 0x20; payload[5] = 0x00; payload[6] = 0x00;
        // Vals: 01
        payload[7] = 0x01;
        payloadLen = 8;
    } 
    else if (controlCode == 0x00) {
        // 关闭 (举例，发 0x00 代表关)
        payload[0] = 0x00; payload[1] = 0x00; payload[2] = 0x00; payload[3] = 0x00;
        payload[4] = 0x20; payload[5] = 0x00; payload[6] = 0x00;
        payload[7] = 0x00;
        payloadLen = 8;
    }
    else {
        // 其他透传逻辑... 暂不处理
        return;
    }
    
    sendToMCU(0x03, 0x11, payload, payloadLen);
}

// ==========================================
// 6. BLE 接收回调：处理来自小程序的数据
// ==========================================
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();

        if (rxValue.length() > 0) {
            Serial.println("*********");
            Serial.print("Received Value: ");
            for (int i = 0; i < rxValue.length(); i++) {
                Serial.printf("%02X ", rxValue[i]);
            }
            Serial.println();
            Serial.println("*********");
            
            // 将接收到的第一个字节作为简易控制码处理
            handleControlCommand(rxValue[0]);
        }
    }
};

// ==========================================
// 7. BLE 连接状态回调
// ==========================================
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device Connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Device Disconnected");
    }
};

// ==========================================
// 8. 串口接收任务：处理 MCU 发来的状态包
// ==========================================
void processMCUData() {
    // 简易的状态机解析
    static uint8_t rxBuffer[128];
    static size_t rxIndex = 0;
    static bool inPacket = false;
    static bool lastWasFF = false;
    
    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        
        if (lastWasFF && b == 0xFF) {
            // 收到完整的包头 FF FF
            inPacket = true;
            rxIndex = 0;
            lastWasFF = false;
            continue;
        }
        
        if (b == 0xFF) {
            lastWasFF = true;
            continue; // 等待下一个字节确认是不是包头
        } else {
            lastWasFF = false;
        }
        
        if (inPacket) {
            // 处理 FF 55 转义机制 (如果是 55 且上一个是 FF，直接丢弃 55)
            // 为了简单起见，这里的 Demo 暂时不做严格的 55 剔除，您可以根据需要完善
            rxBuffer[rxIndex++] = b;
            
            // 如果收到了足够多的数据 (判断长度)
            if (rxIndex >= 2) {
                uint16_t expectedLen = (rxBuffer[0] << 8) | rxBuffer[1];
                if (rxIndex >= expectedLen + 2) { // 包含长度自身的2字节
                    // 包接收完毕
                    uint8_t cmd = rxBuffer[2];
                    uint8_t sn = rxBuffer[3];
                    
                    Serial.printf("MCU Packet Received. Cmd: 0x%02X\n", cmd);
                    
                    // 如果收到状态上报包 (0x05)
                    if (cmd == 0x05) {
                        // 1. 立刻回复 ACK (0x06)
                        sendToMCU(0x06, 0x00, NULL, 0);
                        
                        // 2. 将状态推送到小程序 (简单起见，直接把整个包推过去)
                        if (deviceConnected) {
                            pTxCharacteristic->setValue(rxBuffer, rxIndex);
                            pTxCharacteristic->notify();
                        }
                    }
                    inPacket = false;
                    rxIndex = 0;
                }
            }
        }
    }
}

// ==========================================
// Setup & Loop
// ==========================================
void setup() {
    Serial.begin(115200);
    Serial.println("Starting BLE work!");

    // 1. 初始化串口 1 (连接沙发)
    Serial1.begin(MCU_BAUD, SERIAL_8N1, MCU_RX_PIN, MCU_TX_PIN);

    // 2. 初始化蓝牙服务
    BLEDevice::init("Sofa_Control_ESP32");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    // 创建 TX 特征值 (用于发送通知给小程序)
    pTxCharacteristic = pService->createCharacteristic(
                                        CHARACTERISTIC_UUID_TX,
                                        BLECharacteristic::PROPERTY_NOTIFY
                                    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    // 创建 RX 特征值 (用于接收小程序控制指令)
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                             CHARACTERISTIC_UUID_RX,
                                             BLECharacteristic::PROPERTY_WRITE
                                         );
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("Waiting a client connection to notify...");
    
    // 3. 上电自动发送初始化握手包 (0x01)
    delay(1000);
    sendToMCU(0x01, 0x00, NULL, 0);
}

void loop() {
    // 1. 处理串口数据
    processMCUData();
    
    // 2. 维持心跳保活 (每50秒发送一次 0x08)
    if (millis() - lastHeartbeatTime > HEARTBEAT_INTERVAL) {
        lastHeartbeatTime = millis();
        sendToMCU(0x08, 0x00, NULL, 0);
        Serial.println("Sent Heartbeat (0x08) to MCU");
    }

    // 3. 处理蓝牙断开重连逻辑
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // 给蓝牙堆栈一点时间准备
        pServer->startAdvertising(); // 重启广播
        Serial.println("start advertising");
        oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) {
        // 设备刚刚连接
        oldDeviceConnected = deviceConnected;
    }
    
    delay(10);
}
