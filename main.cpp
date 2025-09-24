#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "AiEsp32RotaryEncoder.h"
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define SERVICE_UUID_2        "12345678-1234-1234-1234-123456789002"
#define CHARACTERISTIC_UUID_2 "87654321-4321-4321-4321-210987654322"
BLECharacteristic* pCharacteristic2 = nullptr;
#define YELLOW_LED_PIN 12 //黃色LED
#define WHITE_LED_PIN 13  //白色LED
#define BUTTON 23 //觸摸開關1 開關
#define BUTTON2 2 //觸摸開關2 色溫 亮度
#define BUTTON3 27 //觸摸開關3 追日
#define BUTTON4 21 //觸摸開關4 番茄
#define BUTTON5 4 //觸摸開關5 自動亮度
#define sensor 33 //光敏電阻
#define CLK 35 //CLK
#define DT 34 //DT
StaticJsonDocument<200> jsonDoc;
String json;

BLEClient* pClient;
BLERemoteCharacteristic* pRemoteCharacteristic;
BLEAdvertisedDevice* targetDevice = nullptr;
BLEScan* pBLEScan;
bool doConnect = false;
bool connected = false;//client



bool f6=0;//debug旗號

int value=0;
int ltemp;
int i=0;
int arr[300]={0};
long avg_value;
int timer3=0;
//旋轉編碼
int count = 0;
int lastCLK = 0;
int l=0;

int f=0;//開關
int f2=0;//色溫亮度
bool f3=0;//追日
bool f4=0;//番茄
bool f5=0;//自動亮度
int ttime;
int btime=10;
int wtime=30;

int light=255;
float ctemp=50;
int timer=0;
int timer2=0;

int sunr=0;
int morn=8;
int noon=24;
int after=40;
int sund=48;
int stemp;
//追日
int aleatorio;
String alea = "2";
String d="";
int flag=0;
int flag2=0;
String handle;
String data;
BLECharacteristic* pCharacteristic = NULL;
BLEServer* pServer = NULL;
bool state=0;//燈狀態
int temp = 0;
int buttonState = 0;
int button2State = 0;
int button3State = 0;
int button4State = 0;
int button5State = 0;
int lastButtonState = 0;
int mode = 0;

bool bf=0;
class MyServerCallbacks: public BLEServerCallbacks{
  void onConnect(BLEServer* pServer) {
      flag2=1;
      
    };
    void onDisconnect(BLEServer* pServer) {
      flag2=0;
      bf=1;
    };
};
void IRAM_ATTR isr(){
      buttonState=1;
}
void IRAM_ATTR isr2(){
      button2State=1;
}
void IRAM_ATTR isr3(){
      button3State=1;
}
void IRAM_ATTR isr4(){
      button4State=1;
}
void IRAM_ATTR isr5(){
      button5State=1;
}
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if(value.length() > 0) {
        DynamicJsonDocument doc(200);
        DeserializationError error = deserializeJson(doc, value);
        if (error) {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.c_str());
          return;
        }
        String temp=doc["handle"];
        handle=temp;
        String temp2=doc["data"];
        data=temp2;
        flag=1;
      }
    }
};
void ClockChanged()   //旋轉編碼
{   
  int clkValue = digitalRead(CLK);  
  int dtValue = digitalRead(DT);    
  if (lastCLK != clkValue)
  {
    lastCLK = clkValue;
    count += (clkValue != dtValue ? 1 : -1); 
    if(l!=count/2){
      f=1;
    }
    l= count/2;
  }
}
void send(String a,String b){
  jsonDoc["handle"] = a;
  jsonDoc["data"] = b;
  serializeJson(jsonDoc, json);
  pCharacteristic->setValue(json.c_str());
  pCharacteristic->notify();
}
void open(int bright,float ctemp){
      if(bright==0){
        analogWrite(YELLOW_LED_PIN, 0);
        analogWrite(WHITE_LED_PIN, 0);
      }else{
        float temp=ctemp/100.0;
        float temp2=1.0-temp;
        int w=bright*temp;
        int y=bright*temp2;
        analogWrite(YELLOW_LED_PIN, y);
        analogWrite(WHITE_LED_PIN, w);
      }
}
bool tf=0;
bool tf2=0;
bool df=0;
bool sf=0;

void timer0(){
  timer++;
  tf=1;
  df=1;
}//時間
void timer4(){
  sf=1;
}//時間
hw_timer_t *tim0=NULL;
hw_timer_t *tim4=NULL;
void Init_timer(){
  tim0=timerBegin(0,80,true);
  timerAttachInterrupt(tim0,timer0,true);
  timerAlarmWrite(tim0,1000000,true);
  timerAlarmEnable(tim0);
  tim4=timerBegin(2,80,true);
  timerAttachInterrupt(tim4,timer4,true);
  timerAlarmWrite(tim4,100000,true);
  timerAlarmEnable(tim4);
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
          Serial.print("Found target device: ");
          Serial.println(advertisedDevice.getAddress().toString().c_str());

          targetDevice = new BLEAdvertisedDevice(advertisedDevice);
          pBLEScan->stop();
          doConnect = true;
      }
  }
};//client

bool connectToServer() {
  Serial.println("Connecting to BLE Server...");

  pClient = BLEDevice::createClient();
  if (!pClient->connect(targetDevice)) {
      Serial.println("Failed to connect.");
      return false;
  }

  Serial.println("Connected!");

  // 尋找 Service
  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID_2);
  if (pRemoteService == nullptr) {
      Serial.println("Service not found.");
      pClient->disconnect();
      return false;
  }

  // 尋找 Characteristic
  pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID_2);
  if (pRemoteCharacteristic == nullptr) {
      Serial.println("Characteristic not found.");
      pClient->disconnect();
      return false;
  }

  Serial.println("Characteristic found!");
  return true;
}// 連接到目標 Server






void setup() {
  Serial.begin(115200);
  Serial.println("device online");
  BLEDevice::init("Rodino_smart_light");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
   pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE|
                                         BLECharacteristic::PROPERTY_NOTIFY
                                       );
  BLEService* pService2 = pServer->createService(SERVICE_UUID_2);
    pCharacteristic2 = pService2->createCharacteristic(
        CHARACTERISTIC_UUID_2,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(WHITE_LED_PIN, OUTPUT);
  pinMode(BUTTON, INPUT);
  pinMode(BUTTON2, INPUT);
  pinMode(BUTTON3, INPUT);
  pinMode(BUTTON4, INPUT);
  pinMode(BUTTON5, INPUT);
  attachInterrupt(BUTTON , isr, HIGH);
  attachInterrupt(BUTTON2 , isr2, HIGH);
  attachInterrupt(BUTTON3 , isr3, HIGH);
  attachInterrupt(BUTTON4 , isr4, HIGH);
  attachInterrupt(BUTTON5 , isr5, HIGH);
  attachInterrupt(digitalPinToInterrupt(CLK), ClockChanged, CHANGE);  
  Init_timer();

  pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
}
void loop()
{
  if(flag2&&sf==1){
    sf=0;
    send("2",String(light));
    send("4",String(ctemp));
    send("3",String(state));
  }
  if(flag2==0&&bf==1){
    pServer->startAdvertising();
    bf=0;
  }
  if(f==1){
      Serial.print("count:");
      Serial.println(l);
      f5=0;
      if(l>0){
        if(f2==0){
          light+=5;
        }else{
          ctemp+=2;
        }
        count=0;
      }else if(l<0){
        count=0;
        if(f2==0){
          light-=5;
        }else{
          ctemp-=2;
        }
      }
      f=0;
      if(ctemp<0) ctemp=0;
      if(ctemp>100) ctemp=100;
      if(light<0) light=0;
      if(light>255) light=255;
      if(state==1){
        open(light,ctemp);
      }
  }
  if(flag){
    flag=0;
   Serial.print("handle: ");
   Serial.println(handle.toInt());
   Serial.println("data："+data);
   switch (handle.toInt())
   {
    case 3:
      if(data=="on"){
       open(light,ctemp);
       state=1;
       send("99","ok");
      }else if(data=="off"){
                            open(0,0);
                            state=0;
                            send("99","ok");
                          }
         break;
    case 5:
      timer=data.toInt();
      send("99","ok");
    case 2:
      light=data.toInt();
      if(state==1){
        open(light,ctemp);
      }
      send("99","ok");
         break;
    case 4:
      ctemp=data.toInt();
      if(state==1){
        open(light,ctemp);
      }
      send("99","ok");
        break;
    case 6:
      sunr=data.toInt();
      send("99","ok");
      break;
    case 7:
      sund=data.toInt();
      send("99","ok");
      break;
      case 10:
        wtime=data.toInt();
        send("99","ok");
      break;
      case 11:
        btime=data.toInt();
        send("99","ok");
      break;
    case 12:
      if(data=="on"){
          if(f4==0){
            button4State=1;
          }
        }else if(data=="off"){
          f4=0;
        }
      send("99","ok");
    break;
    case 89:
        f6=data.toInt();
        break;
    case 8:
      f5=data.toInt();
      send("99","ok");
      break;
      default:
           send("99","error"); 
           break;
          }
       }
  if(buttonState==1){
    if(state==0){
        open(light,ctemp);
        state=1;
    }else{
           open(0,0);
           state=0;
        }
        buttonState=0;
      }
  if(button2State==1){
    if(f2==0){
      f2=1;
      Serial.println("f2=1");
    }else {
      f2=0;
      Serial.println("f2=0");
    }
    button2State=0;
  }
  if(button3State==1){
    button3State=0;
    if(f3==1){
      f3=0;
    }else if(f3==0){
      stemp=sund-sunr;
      morn=sunr+(stemp*(1/6));
      noon=morn+(stemp*(2/6));
      after=noon+(stemp*(2/6));
      f3=1;
    }
  }
  if(f3==1&&state==1){
    if(tf==1){
      tf=0;
      if(timer<=morn){
        int t=morn-timer;
        float t2=1.0-(float(t)/float(morn));
        ctemp=48*t2;
        open(light,ctemp);//日出到早上
      }else if(timer<=noon){
        int t=noon-timer;
        float t2=1-(float(t)/(noon-morn));
        ctemp=48+(t2*28.8);
        open(light,ctemp);//早上到中午
      }else if(timer<=after){
        int t=after-timer;
        float t2=1-(float(t)/(after-noon));
        if(t2<=0.5){
          ctemp=76.8+(23*t2*2);
        }else {
          t2=t2-0.5;
          ctemp=100-(28*t2*2);
        }
        open(light,ctemp);//中午到下午
      }else if(timer<=sund){
        int t=sund-timer;
        float t2=(float(t)/(sund-after));
        ctemp=70*t2;
        open(light,ctemp);//下午到日落
      }else if(timer>=sund){
        f3=0;
      }
      Serial.print("ctemp ");
      Serial.println(ctemp);
      Serial.print("timer ");
      Serial.println(timer);
    }  
  }
  if(button4State==1){
    button4State=0;
    if(f4==0){
      f4=1;
      ttime=timer;
      f3=0;
      open(light,ctemp);
      state=1;
    }else if(f4==1){
      f4=0;
    }
    Serial.print("f4 ");
    Serial.println(f4);
  }
  if(f4==1){
    if(timer>=ttime+btime+wtime){
      open(light,ctemp);
      state=1;
      ttime=timer;
    }else if(timer>=ttime+wtime){
      if(tf==1){
        open(0,0);
        Serial.println("00000000");
        state=0;
      }
    }
    if(tf==1){
      tf=0;
      Serial.print("state ");
      Serial.println(state);
      Serial.print("timer ");
      Serial.println(timer);
    }
  }
  if(button5State==1){
    button5State=0;
    if(f5==0){
     f5=1;
    }else if(f5==1){
     f5=0;
    }
  }
  if(f5==1&&state==1){//自動亮度
        if(timer2>20){
          timer2=0;
          arr[i]= analogRead(33);
          if(arr[i]>255) arr[i]=255;
          i++;
          if(i>=300){
            i=0;
          }
        }
        if(timer3>6000){
          ltemp=value;
          value=0;
          for(int k=0;k<300;k++){
            value=arr[k]+value;
          }
          value = map(value, 0, 153600, 0, 30);
          value = map(value, 0, 30, 20, 255);
          if(ltemp==value){

          }else{
            Serial.print("light ");
            Serial.println(light);
            light=value;
            open(light,ctemp);
          }
         timer3=0;
        }
  }
  if(df==1&&f6==1){
    df=0;
    Serial.print("state ");
    Serial.println(state);
    Serial.print("light ");
    Serial.println(light);
    Serial.print("ctemp ");
    Serial.println(ctemp);
    Serial.print("f2 ");
    Serial.println(f2);
    Serial.print("f3 ");
    Serial.println(f3);
    Serial.print("f4 ");
    Serial.println(f4);
    Serial.print("f5 ");
    Serial.println(f5);
  }
  timer2++;
  timer3++;

}

