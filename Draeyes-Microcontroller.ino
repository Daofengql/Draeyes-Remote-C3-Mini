#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <esp_wifi.h> 
#include "RealisticEyeAnimation.h" // 引入自动眨眼逻辑

// -----------------------------------------------------------------
// 1. 引脚定义
// -----------------------------------------------------------------
#define BTN_UP     3
#define BTN_CENTER 4
#define BTN_DOWN   7
#define BTN_LEFT   6
#define BTN_RIGHT  5

// -----------------------------------------------------------------
// 2. ESP-NOW 配置
// -----------------------------------------------------------------
uint8_t peerMacAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
int espnow_channel = 1;
const int JSON_DOC_SIZE = 160; 

// -----------------------------------------------------------------
// 3. 逻辑变量和常量
// -----------------------------------------------------------------
// --- 定时器 ---
const int LOOP_INTERVAL = 25; // 25ms (40 FPS) 循环间隔
static unsigned long lastLoopRunTime = 0;
unsigned long lastDebugPrintTime = 0;
const int DEBUG_PRINT_INTERVAL = 500; // 降低打印频率以便观察

// --- 状态 ---
EyeState eyeState; 
enum ControlMode {
  AUTO_BLINK_MODE,          // 0: 手动XY + 自动眨眼
  AUTO_FULL_MODE,           // 1: 全自动XY + 自动眨眼
  LOCKED_XY_AUTO_BLINK,     // 2: 锁定XY + 自动眨眼
  LOCKED_XY_MANUAL_BLINK    // 3: 锁定XY + 手动眨眼 (需中键锁定)
};
ControlMode currentMode = AUTO_BLINK_MODE; 
bool centerButtonLocked = false; 
float lockedX = 0.0f;
float lockedY = 0.0f;
float manualBlinkValue = 1.0f; 

// --- 配对 ---
static unsigned long pairButtonPressStart = 0;
static bool pairButtonActionDone = false;
const long LONG_PRESS_DURATION = 2000; 

// --- 中键锁定切换 ---
static unsigned long centerLockPressStart = 0;
static bool centerLockActionDone = false;

// --- 中键连按 (模式切换) ---
static unsigned long lastPressTimeCenter = 0;
static int pressCountCenter = 0;
static bool lastStateCenter = HIGH; // 假设初始状态是松开的 (HIGH)

// --- XY 步进调整 ---
const float XY_STEP = 0.04f; 
const unsigned long LONG_PRESS_INITIAL_DELAY = 400; 
const unsigned long CONTINUOUS_STEP_INTERVAL = 80;  

static unsigned long directionPressStart[4] = {0}; 
static unsigned long lastContinuousStepTime[4] = {0};
static bool directionHeld[4] = {false}; 
static bool lastDirectionState[4] = {HIGH}; // 假设初始状态是松开的 (HIGH)

/** @brief ESP-NOW 数据发送回调 */
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) { /* Serial.println("ESP-NOW fail"); */ }
}

/** @brief 初始化 ESP-NOW */
void setup_espnow() {
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW Init Failed"); return; }
  esp_now_register_send_cb(OnDataSent);
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, peerMacAddress, 6);
  peerInfo.channel = espnow_channel;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) { Serial.println("Add Peer Failed"); }
}

/** @brief 浮点数映射 */
float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
    // 增加保护，防止除以零
    if (in_max == in_min) {
        return out_min; // 或者返回一个中间值，例如 (out_min + out_max) / 2.0f
    }
    float result = (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    // 增加约束，确保结果在输出范围内
    return constrain(result, min(out_min, out_max), max(out_min, out_max));
}


/** @brief 切换控制模式 (循环切换4种) */
void cycleControlMode() {
  currentMode = (ControlMode)(((int)currentMode + 1) % 4); 
  switch(currentMode) {
    case AUTO_BLINK_MODE: Serial.println("模式切换: [手动XY + 自动眨眼]"); break;
    case AUTO_FULL_MODE: Serial.println("模式切换: [全自动XY + 自动眨眼]"); break;
    case LOCKED_XY_AUTO_BLINK: Serial.println("模式切换: [锁定XY + 自动眨眼]"); break;
    case LOCKED_XY_MANUAL_BLINK: Serial.println("模式切换: [锁定XY + 手动眨眼]"); break;
  }
}

/** @brief 切换中键锁定状态 */
void toggleCenterButtonLock() {
    centerButtonLocked = !centerButtonLocked;
    Serial.print("中键锁定状态: ");
    Serial.println(centerButtonLocked ? "已锁定 (Mode 4 可手动眨眼)" : "未锁定 (可三击切换模式)");
}

/** @brief 处理XY步进调整 (单次和长按连续) */
void handleXYAdjustment(bool pressed, int directionIndex, float* axis, float step) {
    unsigned long now = millis();
    bool isActive = pressed; // pressed is already LOW when active

    // --- 按键按下 (下降沿) ---
    // 注意：这里的 lastDirectionState 存储的是 PULLUP 状态 (HIGH=松开, LOW=按下)
    if (isActive && lastDirectionState[directionIndex] == HIGH) {
        *axis += step;
        *axis = constrain(*axis, -1.0f, 1.0f); 
        directionPressStart[directionIndex] = now; 
        directionHeld[directionIndex] = false; 
        lastContinuousStepTime[directionIndex] = now; 
    } 
    // --- 长按检测 ---
    else if (isActive && !directionHeld[directionIndex]) { 
        if (now - directionPressStart[directionIndex] >= LONG_PRESS_INITIAL_DELAY) {
            directionHeld[directionIndex] = true; 
            lastContinuousStepTime[directionIndex] = now; 
        }
    } 
    // --- 连续步进 ---
    else if (isActive && directionHeld[directionIndex]) { 
        if (now - lastContinuousStepTime[directionIndex] >= CONTINUOUS_STEP_INTERVAL) {
             *axis += step;
             *axis = constrain(*axis, -1.0f, 1.0f);
             lastContinuousStepTime[directionIndex] = now; 
        }
    } 
    // --- 按键释放 ---
    else if (!isActive && lastDirectionState[directionIndex] == LOW) {
        directionPressStart[directionIndex] = 0;
        directionHeld[directionIndex] = false;
    }

    lastDirectionState[directionIndex] = isActive ? LOW : HIGH; // 更新上一帧状态
}


// -----------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  setup_espnow();
  randomSeed(analogRead(0));
  init_eye_state(&eyeState);

  Serial.println("ESP32-C3 Preset Controller Initialized.");
  Serial.println("Default Mode: [Manual XY + Auto Blink]");
  Serial.println("Triple-press [Center] to cycle modes.");
  Serial.println("Long-press [Left] + [Right] (2s) to pair.");
  Serial.println("Long-press [Up] + [Down] (2s) to toggle Center Button Lock.");
  
  lastLoopRunTime = millis(); 
}

// -----------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  // --- 25ms 周期控制 ---
  if (now - lastLoopRunTime < LOOP_INTERVAL) { delay(1); return; }
  lastLoopRunTime = now;

  // --- 1. 读取按键状态 ---
  bool upPressed     = (digitalRead(BTN_UP)     == LOW);
  bool centerPressed = (digitalRead(BTN_CENTER) == LOW);
  bool downPressed   = (digitalRead(BTN_DOWN)   == LOW);
  bool leftPressed   = (digitalRead(BTN_LEFT)   == LOW);
  bool rightPressed  = (digitalRead(BTN_RIGHT)  == LOW);

  // --- 2. 处理组合键 & 模式切换 ---
  bool onlyPairKeys = leftPressed && rightPressed && !upPressed && !downPressed && !centerPressed;
  bool onlyCenterLockKeys = upPressed && downPressed && !leftPressed && !rightPressed && !centerPressed;

  // A. 配对 (Left + Right 长按)
  if (onlyPairKeys) {
    if (pairButtonPressStart == 0) pairButtonPressStart = now;
    if (!pairButtonActionDone && (now - pairButtonPressStart >= LONG_PRESS_DURATION)) {
      Serial.println("Sending Pairing Request...");
      StaticJsonDocument<JSON_DOC_SIZE> pairDoc; pairDoc["req"] = "pairing";
      char pairBuffer[64]; size_t pairLen = serializeJson(pairDoc, pairBuffer, sizeof(pairBuffer));
      esp_now_send(peerMacAddress, (uint8_t *)pairBuffer, pairLen);
      pairButtonActionDone = true; 
    }
  } else { pairButtonPressStart = 0; pairButtonActionDone = false; }

  // B. 中键锁定切换 (Up + Down 长按)
  if (onlyCenterLockKeys) {
      if (centerLockPressStart == 0) centerLockPressStart = now;
      if (!centerLockActionDone && (now - centerLockPressStart >= LONG_PRESS_DURATION)) {
          toggleCenterButtonLock();
          centerLockActionDone = true; 
      }
  } else { centerLockPressStart = 0; centerLockActionDone = false; }

  // C. 模式切换 (Center 连按三次) - 仅在非组合键、非中键锁定模式时检测
  // -------------------------------------------------------------
  // -- 已应用修正 --
  // -------------------------------------------------------------
  if (!onlyPairKeys && !onlyCenterLockKeys && !centerButtonLocked) {
    bool currentCenterStateIsPressed = centerPressed; // 读取 PULLUP 状态 (LOW = 按下)
    
    // lastStateCenter 存储的是 PULLUP 状态 (HIGH = 松开, LOW = 按下)
    // 检查下降沿 (从 HIGH 变为 LOW)
    if (currentCenterStateIsPressed && lastStateCenter == HIGH) { 
      if (now - lastPressTimeCenter <= 1000) {
        pressCountCenter++; 
      } else {
        pressCountCenter = 1;
      }
      lastPressTimeCenter = now;
      
      if (pressCountCenter >= 3) { 
        cycleControlMode(); 
        pressCountCenter = 0; 
      }
    }
    // 更新上一帧状态
    lastStateCenter = currentCenterStateIsPressed ? LOW : HIGH; 
  } else if (centerButtonLocked) {
      // 如果处于中键锁定状态，重置连按计数器，防止解锁后立即触发
      pressCountCenter = 0;
      // 并且确保 lastStateCenter 反映当前真实状态 (松开)
      if (!centerPressed) {
          lastStateCenter = HIGH;
      }
  }
  
  // --- 3. 根据当前模式处理输入和状态 ---
  if (!onlyPairKeys && !onlyCenterLockKeys) {
      
      float currentX = 0.0f;
      float currentY = 0.0f;
      float currentL = 1.0f;
      float currentR = 1.0f;
      float rawAnimX = 0.0f; // For debugging AUTO mode
      float rawAnimY = 0.0f; // For debugging AUTO mode

      // 更新自动眨眼状态 (大多数模式需要)
      update_auto_blink(&eyeState);
      update_blink_state(&eyeState);

      switch(currentMode) {
          case AUTO_BLINK_MODE:
              // 手动8向移动
              if (leftPressed && !rightPressed) currentX = -1.0f;
              else if (rightPressed && !leftPressed) currentX = 1.0f;
              else currentX = 0.0f; // Ensure return to 0 if both or neither pressed

              if (upPressed && !downPressed) currentY = 1.0f;
              else if (downPressed && !upPressed) currentY = -1.0f;
              else currentY = 0.0f; // Ensure return to 0

              if (centerPressed && !centerButtonLocked) { currentX = 0.0f; currentY = 0.0f; } // 中键归中优先 (仅在未锁定时)
              
              currentL = eyeState.eyeOpenness;
              currentR = eyeState.eyeOpenness;
              break;
          
          case AUTO_FULL_MODE:
              // 全自动移动和眨眼
              update_auto_movement(&eyeState, 3000); 
              rawAnimX = eyeState.eyeCurrentX; // Store raw value for debug
              rawAnimY = eyeState.eyeCurrentY; // Store raw value for debug
              currentX = fmap(rawAnimX, 60.0f, 180.0f, -1.0f, 1.0f);
              currentY = fmap(rawAnimY, 60.0f, 180.0f, -1.0f, 1.0f);
              currentL = eyeState.eyeOpenness;
              currentR = eyeState.eyeOpenness;
              break;

          case LOCKED_XY_AUTO_BLINK:
          case LOCKED_XY_MANUAL_BLINK:
              // 处理锁定XY的调整 (此时按键不应触发模式A的移动)
              handleXYAdjustment(upPressed,    0, &lockedY,  XY_STEP); 
              handleXYAdjustment(downPressed,  1, &lockedY, -XY_STEP); 
              handleXYAdjustment(leftPressed,  2, &lockedX, -XY_STEP); 
              handleXYAdjustment(rightPressed, 3, &lockedX,  XY_STEP); 
              
              currentX = lockedX;
              currentY = lockedY;

              if (currentMode == LOCKED_XY_AUTO_BLINK) {
                  currentL = eyeState.eyeOpenness;
                  currentR = eyeState.eyeOpenness;
              } else { // LOCKED_XY_MANUAL_BLINK
                  if (centerButtonLocked) {
                      // 只有在中键锁定模式下，中键才用于手动眨眼
                      manualBlinkValue = centerPressed ? 0.0f : 1.0f; 
                  } else {
                      manualBlinkValue = 1.0f; // 非锁定状态，保持睁眼
                  }
                  currentL = manualBlinkValue;
                  currentR = manualBlinkValue;
              }
              break;
      }

      // --- 4. 打包并发送数据 ---
      StaticJsonDocument<JSON_DOC_SIZE> doc;
      char jsonBuffer[256];
      doc.clear(); 
      doc["req"] = "controller";
      JsonObject dataObj = doc.createNestedObject("data");
      dataObj["type"] = "preset"; 
      dataObj["j1PotX"] = currentX;
      dataObj["j1PotY"] = currentY;
      dataObj["bkl"] = currentL;
      dataObj["bkr"] = currentR;
      size_t jsonLength = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
      
      // --- 5. 增强调试打印 ---
      if (now - lastDebugPrintTime >= DEBUG_PRINT_INTERVAL) {
          lastDebugPrintTime = now;
          Serial.print("--- Debug --- Mode: "); Serial.print(currentMode); 
          Serial.print(" | C-Lock: "); Serial.print(centerButtonLocked);
          Serial.print(" | Btns(U D L R C): "); 
          Serial.print(upPressed); Serial.print(downPressed); Serial.print(leftPressed); Serial.print(rightPressed); Serial.print(centerPressed);
          
          if (currentMode == AUTO_FULL_MODE) {
             Serial.print(" | RawAnim(X,Y): "); Serial.print(rawAnimX, 1); Serial.print(","); Serial.print(rawAnimY, 1);
          } else if (currentMode == LOCKED_XY_AUTO_BLINK || currentMode == LOCKED_XY_MANUAL_BLINK) {
             Serial.print(" | LockXY(X,Y): "); Serial.print(lockedX, 2); Serial.print(","); Serial.print(lockedY, 2);
          }
          
          Serial.print(" | Sent(X,Y): "); Serial.print(currentX, 2); Serial.print(","); Serial.print(currentY, 2); 
          Serial.print(" | Blink(L,R): "); Serial.print(currentL, 2); Serial.print(","); Serial.println(currentR, 2);
          // Serial.println(jsonBuffer); // Optional: print full JSON too
          Serial.println("-------------");
      }
      
      esp_now_send(peerMacAddress, (uint8_t *)jsonBuffer, jsonLength);
  } 
}