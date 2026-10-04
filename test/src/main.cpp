#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "AiEsp32RotaryEncoder.h"
// ==========================================
// 系統常數與腳位定義 
// ==========================================
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define SERVICE_UUID_2        "12345678-1234-1234-1234-123456789002"
#define CHARACTERISTIC_UUID_2 "87654321-4321-4321-4321-210987654322"

constexpr int PIN_LED_Y    = 12; //黃色LED
constexpr int PIN_LED_W    = 13;  //白色LED
constexpr int PIN_BTN_PWR  = 23; //觸摸開關1 開關
constexpr int PIN_BTN_MODE = 2; //觸摸開關2 色溫 亮度
constexpr int PIN_BTN_SUN  = 27; //觸摸開關3 追日
constexpr int PIN_BTN_TOMO = 21; //觸摸開關4 番茄
constexpr int PIN_BTN_AUTO = 4; //觸摸開關5 自動亮度
constexpr int PIN_LDR      = 33; //光敏電阻
constexpr int PIN_ENC_CLK  = 35; //CLK
constexpr int PIN_ENC_DT   = 34; //DT

// ==========================================
// 系統狀態與資料結構
// ==========================================
struct LampSettings {
    bool isOn = false;        //燈狀態
    int brightness = 255;     //亮度
    float colorTemp = 50.0;   //色溫
};

//功能狀態旗號
struct SystemModes {
    bool encoderControlsCT = false; //色溫亮度
    bool sunTracking = false;       //追日
    bool pomodoro = false;          //番茄
    bool autoBrightness = false;    //自動亮度
    bool debug = false;             //debug旗號
};
//蕃茄鐘持續時間設定變數
struct PomodoroTimer {
    int startTime = 0;  //ttime
    int workTime = 30;  //wtime
    int breakTime = 10; //btime
};
//太陽狀態變數
struct SunTrackingTimer {
    int sunrise = 0;    //sunr
    int sundown = 48;   //sund
    int morning = 8;    //morn
    int noon = 24;      //noon
    int afternoon = 40; //after
};

LampSettings lamp;
SystemModes modes;
PomodoroTimer pomodoro;
SunTrackingTimer sun;

// ==========================================
// 全局計時器與旗標
// ==========================================
volatile int systemSeconds = 0;          //timer
volatile bool flagOneSec = false;        //Debug 資訊輸出flag
volatile bool flagBleUpdate = false;     //ble檯燈狀態更新
volatile bool isClientConnected = false; //手機有無連線
volatile bool needsReAdvertise = false;  //廣播是否啟動旗號

// ==========================================
// 旋轉編碼器與按鈕旗標
// ==========================================
//旋轉編碼
volatile int encCount = 0;       //count
volatile int encLastClk = 0;     //lastCLK
int encPosition = 0;             //l
bool encChanged = false;         //開關

volatile bool btnPwrPressed = false;   //buttonState
volatile bool btnModePressed = false;  //button2State
volatile bool btnSunPressed = false;   //button3State
volatile bool btnTomoPressed = false;  //button4State
volatile bool btnAutoPressed = false;  //button5State


// BLE 變數
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
BLECharacteristic* pCharacteristic2 = nullptr;
String bleHandle = "";
String bleData = "";
bool hasNewBleCmd = false;
StaticJsonDocument<200> jsonDoc;

// ==========================================
// 3. 中斷服務函式 (ISR)
// ==========================================
void IRAM_ATTR isrBtnPwr()  { btnPwrPressed = true; }
void IRAM_ATTR isrBtnMode() { btnModePressed = true; }
void IRAM_ATTR isrBtnSun()  { btnSunPressed = true; }
void IRAM_ATTR isrBtnTomo() { btnTomoPressed = true; }
void IRAM_ATTR isrBtnAuto() { btnAutoPressed = true; }

void IRAM_ATTR isrEncoder() {
    int clkValue = digitalRead(PIN_ENC_CLK);
    int dtValue = digitalRead(PIN_ENC_DT);
    if (encLastClk != clkValue) {
        encLastClk = clkValue;
        encCount += (clkValue != dtValue ? 1 : -1);
        if (encPosition != encCount / 2) {
            encChanged = true;
        }
        encPosition = encCount / 2;
    }
}

void IRAM_ATTR isrTimer1Sec() {
    systemSeconds++;
    flagOneSec = true;
}

void IRAM_ATTR isrTimerBle() {
    flagBleUpdate = true;
}

// ==========================================
// BLE 回調與通訊處理 (BLE Callbacks)
// ==========================================
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { isClientConnected = true; }
    void onDisconnect(BLEServer* pServer) {
        isClientConnected = false;
        needsReAdvertise = true;
    }
};

class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            DynamicJsonDocument doc(200);
            DeserializationError error = deserializeJson(doc, value);
            if (error) {
                Serial.println("JSON 解析失敗");
                return;
            }
            bleHandle = doc["handle"].as<String>();
            bleData = doc["data"].as<String>();
            hasNewBleCmd = true;
        }
    }
};

void sendBleMsg(String handle, String data) {
    if (!isClientConnected) return;
    String jsonStr;
    jsonDoc.clear();
    jsonDoc["handle"] = handle;
    jsonDoc["data"] = data;
    serializeJson(jsonDoc, jsonStr);
    pCharacteristic->setValue(jsonStr.c_str());
    pCharacteristic->notify();
}

// ==========================================
// 硬體控制與核心邏輯函式
// ==========================================
void applyLampSettings() {
    if (!lamp.isOn) {
        analogWrite(PIN_LED_Y, 0);
        analogWrite(PIN_LED_W, 0);
    } else {
        float ratioW = lamp.colorTemp / 100.0;
        float ratioY = 1.0 - ratioW;
        int w = lamp.brightness * ratioW;
        int y = lamp.brightness * ratioY;
        analogWrite(PIN_LED_Y, y);
        analogWrite(PIN_LED_W, w);
    }
}

void initTimers() {
    hw_timer_t* tim0 = timerBegin(0, 80, true);
    timerAttachInterrupt(tim0, isrTimer1Sec, true);
    timerAlarmWrite(tim0, 1000000, true); // 1秒
    timerAlarmEnable(tim0);

    hw_timer_t* tim4 = timerBegin(2, 80, true);
    timerAttachInterrupt(tim4, isrTimerBle, true);
    timerAlarmWrite(tim4, 100000, true); // 0.1秒
    timerAlarmEnable(tim4);
}

// 處理自動亮度 
void handleAutoBrightness() {
    if (!modes.autoBrightness || !lamp.isOn) return;

    static unsigned long lastReadTime = 0;
    static unsigned long lastCalcTime = 0;
    static long sumLDR = 0;
    static int readCount = 0;
    static int lastComputedBright = -1;

    // 每 20ms 讀取一次感測器
    if (millis() - lastReadTime > 20) {
        lastReadTime = millis();
        int val = analogRead(PIN_LDR);
        if(val > 255) val = 255;
        sumLDR += val;
        readCount++;
    }

    // 每 6 秒 (600ms) 計算一次平均並調整亮度
    if (millis() - lastCalcTime > 600) {
        lastCalcTime = millis();
        if (readCount > 0) {
            long avg = sumLDR / readCount;
            
            int mappedVal = map(avg * 300, 0, 153600, 0, 30); 
            mappedVal = map(mappedVal, 0, 30, 20, 255);
            
            if (lastComputedBright != mappedVal) {
                lastComputedBright = mappedVal;
                lamp.brightness = mappedVal;
                applyLampSettings();
                Serial.printf("Auto Brightness: %d\n", lamp.brightness);
            }
        }
        sumLDR = 0;
        readCount = 0;
    }
}

// 處理旋轉編碼器
void handleEncoder() {
    if (encChanged) {
        modes.autoBrightness = false; // 手動調整時關閉自動亮度
        if (encPosition > 0) {
            modes.encoderControlsCT ? lamp.colorTemp += 2 : lamp.brightness += 5;
        } else if (encPosition < 0) {
            modes.encoderControlsCT ? lamp.colorTemp -= 2 : lamp.brightness -= 5;
        }
        
        encCount = 0;
        encPosition = 0;
        encChanged = false;

        // 數值邊界限制
        lamp.colorTemp = constrain(lamp.colorTemp, 0.0, 100.0);
        lamp.brightness = constrain(lamp.brightness, 0, 255);

        if (lamp.isOn) applyLampSettings();
    }
}

// 處理實體按鈕
void handleButtons() {
    if (btnPwrPressed) {
        btnPwrPressed = false;
        lamp.isOn = !lamp.isOn;
        applyLampSettings();
    }
    if (btnModePressed) {
        btnModePressed = false;
        modes.encoderControlsCT = !modes.encoderControlsCT;
        Serial.printf("Encoder Mode: %s\n", modes.encoderControlsCT ? "ColorTemp" : "Brightness");
    }
    if (btnSunPressed) {
        btnSunPressed = false;
        modes.sunTracking = !modes.sunTracking;
        if (modes.sunTracking) {
            int timeSpan = sun.sundown - sun.sunrise;
            sun.morning = sun.sunrise + (timeSpan / 6);
            sun.noon = sun.morning + (timeSpan * 2 / 6);
            sun.afternoon = sun.noon + (timeSpan * 2 / 6);
        }
    }
    if (btnTomoPressed) {
        btnTomoPressed = false;
        modes.pomodoro = !modes.pomodoro;
        if (modes.pomodoro) {
            pomodoro.startTime = systemSeconds;
            modes.sunTracking = false;
            lamp.isOn = true;
            applyLampSettings();
        }
    }
    if (btnAutoPressed) {
        btnAutoPressed = false;
        modes.autoBrightness = !modes.autoBrightness;
    }
}

// 處理番茄鐘與追日模式時間邏輯
void handleModesTimeLogic() {
    if (!flagOneSec) return; 
    // flagOneSec 會在下面統一清空

    // --- 追日模式邏輯 ---
    if (modes.sunTracking && lamp.isOn) {
        if (systemSeconds <= sun.morning) {
            float ratio = 1.0 - (float(sun.morning - systemSeconds) / sun.morning);
            lamp.colorTemp = 48.0 * ratio;
        } else if (systemSeconds <= sun.noon) {
            float ratio = 1.0 - (float(sun.noon - systemSeconds) / (sun.noon - sun.morning));
            lamp.colorTemp = 48.0 + (ratio * 28.8);
        } else if (systemSeconds <= sun.afternoon) {
            float ratio = 1.0 - (float(sun.afternoon - systemSeconds) / (sun.afternoon - sun.noon));
            lamp.colorTemp = (ratio <= 0.5) ? 76.8 + (23 * ratio * 2) : 100 - (28 * (ratio - 0.5) * 2);
        } else if (systemSeconds <= sun.sundown) {
            float ratio = float(sun.sundown - systemSeconds) / (sun.sundown - sun.afternoon);
            lamp.colorTemp = 70.0 * ratio;
        } else {
            modes.sunTracking = false;
        }
        applyLampSettings();
    }

    // --- 番茄鐘邏輯 ---
    if (modes.pomodoro) {
        int elapsed = systemSeconds - pomodoro.startTime;
        if (elapsed >= (pomodoro.breakTime + pomodoro.workTime)) {
            lamp.isOn = true;
            applyLampSettings();
            pomodoro.startTime = systemSeconds; // 重新開始循環
        } else if (elapsed >= pomodoro.workTime) {
            if (lamp.isOn) {
                lamp.isOn = false;
                applyLampSettings();
            }
        }
    }
}

// 解析藍牙命令
void processBleCommand() {
    if (!hasNewBleCmd) return;
    hasNewBleCmd = false;

    int handleId = bleHandle.toInt();
    switch (handleId) {
        case 2: // 設定亮度
            lamp.brightness = bleData.toInt();
            if (lamp.isOn) applyLampSettings();
            sendBleMsg("99", "ok");
            break;
        case 3: // 開關
            lamp.isOn = (bleData == "on");
            applyLampSettings();
            sendBleMsg("99", "ok");
            break;
        case 4: // 設定色溫
            lamp.colorTemp = bleData.toInt();
            if (lamp.isOn) applyLampSettings();
            sendBleMsg("99", "ok");
            break;
        case 5: // 同步系統時間
            systemSeconds = bleData.toInt();
            sendBleMsg("99", "ok");
            break;
        case 6: // 日出時間
            sun.sunrise = bleData.toInt();
            sendBleMsg("99", "ok");
            break;
        case 7: // 日落時間
            sun.sundown = bleData.toInt();
            sendBleMsg("99", "ok");
            break;
        case 8: // 自動亮度開關
            modes.autoBrightness = bleData.toInt();
            sendBleMsg("99", "ok");
            break;
        case 10: // 番茄鐘工作時間
            pomodoro.workTime = bleData.toInt();
            sendBleMsg("99", "ok");
            break;
        case 11: // 番茄鐘休息時間
            pomodoro.breakTime = bleData.toInt();
            sendBleMsg("99", "ok");
            break;
        case 12: // 番茄鐘開關
            if (bleData == "on") btnTomoPressed = true;
            else modes.pomodoro = false;
            sendBleMsg("99", "ok");
            break;
        case 89: // Debug 模式
            modes.debug = bleData.toInt();
            break;
        default:
            sendBleMsg("99", "error");
            break;
    }
}


void setup() {
    Serial.begin(115200);
    Serial.println("Device Online");

    // 硬體腳位初始化
    pinMode(PIN_ENC_CLK, INPUT);
    pinMode(PIN_ENC_DT, INPUT);
    pinMode(PIN_LED_Y, OUTPUT);
    pinMode(PIN_LED_W, OUTPUT);
    pinMode(PIN_BTN_PWR, INPUT);
    pinMode(PIN_BTN_MODE, INPUT);
    pinMode(PIN_BTN_SUN, INPUT);
    pinMode(PIN_BTN_TOMO, INPUT);
    pinMode(PIN_BTN_AUTO, INPUT);

    // 中斷設定
    attachInterrupt(PIN_BTN_PWR,  isrBtnPwr, HIGH);
    attachInterrupt(PIN_BTN_MODE, isrBtnMode, HIGH);
    attachInterrupt(PIN_BTN_SUN,  isrBtnSun, HIGH);
    attachInterrupt(PIN_BTN_TOMO, isrBtnTomo, HIGH);
    attachInterrupt(PIN_BTN_AUTO, isrBtnAuto, HIGH);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), isrEncoder, CHANGE);

    // BLE 初始化
    BLEDevice::init("MyESP32");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    BLEService* pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();

    // 啟動廣播
    BLEAdvertising* pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();

    initTimers();
}

void loop() {
    // 處理 BLE 斷線重啟廣播
    if (needsReAdvertise && !isClientConnected) {
        delay(500); // 緩衝一下
        pServer->startAdvertising();
        Serial.println("Restarting BLE Advertising...");
        needsReAdvertise = false;
    }

    // 定期向 App 匯報狀態
    if (isClientConnected && flagBleUpdate) {
        flagBleUpdate = false;
        sendBleMsg("2", String(lamp.brightness));
        sendBleMsg("4", String(lamp.colorTemp));
        sendBleMsg("3", String(lamp.isOn));
    }

    //處理按鈕與旋轉編碼器 
    handleEncoder();
    handleButtons();

    // 處理 BLE 命令
    processBleCommand();

    //處理持續性模式 (番茄鐘、追日、自動亮度)
    handleModesTimeLogic();
    handleAutoBrightness();

    //Debug 資訊輸出
    if (flagOneSec) {
        if (modes.debug) {
            Serial.printf("State:%d Light:%d CT:%.1f Mode:%d Sun:%d Tomo:%d Auto:%d\n", 
                lamp.isOn, lamp.brightness, lamp.colorTemp, 
                modes.encoderControlsCT, modes.sunTracking, modes.pomodoro, modes.autoBrightness);
        }
        flagOneSec = false; 
    }
}
