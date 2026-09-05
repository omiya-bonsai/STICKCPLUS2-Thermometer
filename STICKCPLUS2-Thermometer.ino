#include "M5StickCPlus2.h"
#include <Wire.h>
#include <Adafruit_MLX90614.h>

// ============================================================
// MLX90614
// ============================================================

Adafruit_MLX90614 mlx;

constexpr int SDA_PIN = 33;
constexpr int SCL_PIN = 32;
constexpr uint8_t MLX_ADDR = 0x5A;

constexpr int MLX_INIT_RETRIES = 10;
constexpr uint32_t MLX_INIT_RETRY_DELAY_MS = 300;

// ============================================================
// Timing
// ============================================================

constexpr uint32_t MEASURE_INTERVAL_MS = 150;

constexpr uint32_t HOLD_DURATION_MS =
  60UL * 1000UL;

constexpr uint32_t AUTO_POWER_OFF_MS =
  300UL * 1000UL;

// ============================================================
// Filtering
// ============================================================

constexpr float EMA_ALPHA = 0.35f;

constexpr int MEDIAN_SAMPLES = 5;

// ============================================================
// State machine
// ============================================================

enum class AppState {
  LIVE,
  MEASURING,
  HOLD
};

AppState appState = AppState::LIVE;

// ============================================================
// Temperature
// ============================================================

float rawObjectTemp = NAN;
float filteredObjectTemp = NAN;
float ambientTemp = NAN;

float holdObjectTemp = NAN;
float holdAmbientTemp = NAN;

// ============================================================
// Median ring buffer
// ============================================================

float medianBuffer[MEDIAN_SAMPLES];

int medianIndex = 0;
int medianCount = 0;

// ============================================================
// Timing state
// ============================================================

uint32_t lastMeasureMs = 0;
uint32_t holdStartedMs = 0;
uint32_t lastUserActivityMs = 0;

// ============================================================
// Button state
// ============================================================

bool btnAPrev = false;

// ============================================================
// Sensor state
// ============================================================

bool mlxReady = false;

// ============================================================
// Sprite
// ============================================================

M5Canvas canvas(&StickCP2.Display);

// ============================================================
// I2C device check
// ============================================================

bool isI2CDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

// ============================================================
// MLX90614 initialization
// ============================================================

bool initMLX90614() {

  Serial.println();
  Serial.println("Initializing MLX90614...");

  for (
    int attempt = 1;
    attempt <= MLX_INIT_RETRIES;
    ++attempt) {

    Serial.printf(
      "Attempt %d/%d: ",
      attempt,
      MLX_INIT_RETRIES);

    if (!isI2CDevicePresent(MLX_ADDR)) {

      Serial.println("0x5A not found");

      delay(
        MLX_INIT_RETRY_DELAY_MS);

      continue;
    }

    Serial.print("0x5A found, ");

    if (
      mlx.begin(
        MLX_ADDR,
        &Wire)) {

      Serial.println(
        "MLX90614 OK");

      return true;
    }

    Serial.println(
      "mlx.begin() failed");

    delay(
      MLX_INIT_RETRY_DELAY_MS);
  }

  Serial.println(
    "MLX90614 initialization failed.");

  return false;
}

// ============================================================
// Median buffer
// ============================================================

void resetMedianBuffer() {

  medianIndex = 0;
  medianCount = 0;
}

void addMedianSample(float value) {

  medianBuffer[medianIndex] =
    value;

  medianIndex++;

  if (
    medianIndex >= MEDIAN_SAMPLES) {
    medianIndex = 0;
  }

  if (
    medianCount < MEDIAN_SAMPLES) {
    medianCount++;
  }
}

// ============================================================
// Median
// ============================================================

float getMedian() {

  if (medianCount == 0) {
    return NAN;
  }

  float values[MEDIAN_SAMPLES];

  for (
    int i = 0;
    i < medianCount;
    ++i) {
    values[i] =
      medianBuffer[i];
  }

  for (
    int i = 0;
    i < medianCount - 1;
    ++i) {

    for (
      int j = i + 1;
      j < medianCount;
      ++j) {

      if (
        values[j] < values[i]) {

        float tmp =
          values[i];

        values[i] =
          values[j];

        values[j] =
          tmp;
      }
    }
  }

  if (
    medianCount % 2 == 1) {

    return values[medianCount / 2];
  }

  return (
           values[(medianCount / 2) - 1] + values[medianCount / 2])
         / 2.0f;
}

// ============================================================
// Temperature read
// ============================================================

bool readTemperature() {

  float newObject =
    mlx.readObjectTempC();

  float newAmbient =
    mlx.readAmbientTempC();

  if (
    isnan(newObject) || isnan(newAmbient)) {

    Serial.println(
      "Temperature read failed.");

    return false;
  }

  rawObjectTemp =
    newObject;

  ambientTemp =
    newAmbient;

  // ----------------------------------------------------------
  // MEASURING中だけ中央値用データへ追加
  // ----------------------------------------------------------

  if (
    appState == AppState::MEASURING) {

    addMedianSample(
      rawObjectTemp);
  }

  // ----------------------------------------------------------
  // LIVE / MEASURING表示用EMA
  // ----------------------------------------------------------

  if (
    isnan(filteredObjectTemp)) {

    filteredObjectTemp =
      rawObjectTemp;

  } else {

    filteredObjectTemp =
      EMA_ALPHA * rawObjectTemp + (1.0f - EMA_ALPHA) * filteredObjectTemp;
  }

  Serial.printf(
    "Raw: %.2f C  "
    "Filtered: %.2f C  "
    "Ambient: %.2f C\n",
    rawObjectTemp,
    filteredObjectTemp,
    ambientTemp);

  return true;
}

// ============================================================
// Error screen
// ============================================================

void drawError(
  const char* title,
  const char* subtitle) {

  canvas.fillSprite(BLACK);

  canvas.setTextDatum(
    middle_center);

  canvas.setTextSize(1);

  canvas.setTextFont(
    &fonts::Font2);

  canvas.setTextColor(
    RED,
    BLACK);

  canvas.drawString(
    title,
    canvas.width() / 2,
    canvas.height() / 2 - 12);

  canvas.setTextColor(
    LIGHTGREY,
    BLACK);

  canvas.drawString(
    subtitle,
    canvas.width() / 2,
    canvas.height() / 2 + 14);

  canvas.pushSprite(
    0,
    0);
}

// ============================================================
// Main screen
//
//        26.7
//
// LIVE  Ambient 25.4 C
// MEAS  Ambient 25.4 C
// HOLD  Ambient 25.4 C
//
// ============================================================

void drawTemperatureScreen() {

  canvas.fillSprite(BLACK);

  float shownObject = NAN;
  float shownAmbient = NAN;

  if (
    appState == AppState::HOLD) {

    shownObject =
      holdObjectTemp;

    shownAmbient =
      holdAmbientTemp;

  } else {

    shownObject =
      filteredObjectTemp;

    shownAmbient =
      ambientTemp;
  }

  char buf[32];

  // ==========================================================
  // Main temperature
  // ==========================================================

  if (
    isnan(shownObject)) {

    snprintf(
      buf,
      sizeof(buf),
      "--.-");

  } else {

    snprintf(
      buf,
      sizeof(buf),
      "%.1f",
      shownObject);
  }

  canvas.setTextDatum(
    middle_center);

  canvas.setTextColor(
    WHITE,
    BLACK);

  canvas.setTextFont(
    &fonts::Font7);

  canvas.setTextSize(2);

  // 横幅を超える場合だけ縮小
  if (
    canvas.textWidth(buf) > canvas.width() - 8) {

    canvas.setTextSize(1);
  }

  canvas.drawString(
    buf,
    canvas.width() / 2,
    55);

  // ==========================================================
  // Bottom line
  // ==========================================================

  canvas.setTextSize(1);

  canvas.setTextDatum(
    bottom_center);

  canvas.setTextFont(
    &fonts::Font0);

  const char* stateText = "";

  switch (appState) {

    case AppState::LIVE:

      stateText = "LIVE";

      canvas.setTextColor(
        CYAN,
        BLACK);

      break;

    case AppState::MEASURING:

      stateText = "MEAS";

      canvas.setTextColor(
        GREEN,
        BLACK);

      break;

    case AppState::HOLD:

      stateText = "HOLD";

      canvas.setTextColor(
        YELLOW,
        BLACK);

      break;
  }

  if (
    isnan(shownAmbient)) {

    snprintf(
      buf,
      sizeof(buf),
      "%s  Ambient --.- C",
      stateText);

  } else {

    snprintf(
      buf,
      sizeof(buf),
      "%s  Ambient %.1f C",
      stateText,
      shownAmbient);
  }

  canvas.drawString(
    buf,
    canvas.width() / 2,
    canvas.height() - 2);

  canvas.pushSprite(
    0,
    0);
}

// ============================================================
// Enter LIVE
// ============================================================

void enterLive(
  uint32_t now) {

  appState =
    AppState::LIVE;

  filteredObjectTemp =
    NAN;

  resetMedianBuffer();

  Serial.println(
    "STATE -> LIVE");

  if (
    readTemperature()) {

    drawTemperatureScreen();
  }

  lastMeasureMs =
    now;
}

// ============================================================
// Start MEASURING
//
// BtnA pressed
// ============================================================

void startMeasuring(
  uint32_t now) {

  appState =
    AppState::MEASURING;

  // このボタン押下から
  // 新しい中央値計算を開始
  resetMedianBuffer();

  Serial.println(
    "STATE -> MEASURING");

  // 押した瞬間も1点取得
  if (
    readTemperature()) {

    drawTemperatureScreen();
  }

  lastMeasureMs =
    now;
}

// ============================================================
// Finish MEASURING -> HOLD
//
// BtnA released
// ============================================================

void finishMeasuring(
  uint32_t now) {

  Serial.println(
    "MEASURING -> HOLD");

  // ----------------------------------------------------------
  // 指を離した瞬間の最新サンプルを追加
  //
  // この時点ではまだMEASURINGなので
  // readTemperature() 内で中央値バッファへ入る
  // ----------------------------------------------------------

  bool readOk =
    readTemperature();

  // ----------------------------------------------------------
  // 直近最大5点の中央値
  // ----------------------------------------------------------

  float result =
    getMedian();

  // ----------------------------------------------------------
  // 中央値が取れなかった場合もLIVEへ戻さない
  //
  // 最後の有効値をHOLDする
  // ----------------------------------------------------------

  if (
    isnan(result)) {

    if (
      !isnan(rawObjectTemp)) {

      result =
        rawObjectTemp;

    } else if (
      !isnan(filteredObjectTemp)) {

      result =
        filteredObjectTemp;
    }
  }

  holdObjectTemp =
    result;

  holdAmbientTemp =
    ambientTemp;

  // ----------------------------------------------------------
  // 必ずHOLDへ遷移
  // ----------------------------------------------------------

  appState =
    AppState::HOLD;

  // ----------------------------------------------------------
  // 重要:
  // loop先頭で取得した同じnowを使用
  //
  // holdStartedMs = millis() にはしない
  // ----------------------------------------------------------

  holdStartedMs =
    now;

  Serial.printf(
    "STATE -> HOLD  "
    "result=%.2f C  "
    "samples=%d  "
    "read=%s\n",
    holdObjectTemp,
    medianCount,
    readOk ? "OK" : "FAIL");

  Serial.print(
    "Median buffer:");

  for (
    int i = 0;
    i < medianCount;
    ++i) {

    Serial.printf(
      " %.2f",
      medianBuffer[i]);
  }

  Serial.println();

  // HOLD状態になってから描画
  drawTemperatureScreen();

  StickCP2.Speaker.tone(
    4000,
    50);
}

// ============================================================
// Power Off
// ============================================================

void powerOffDevice() {

  Serial.println(
    "AUTO POWER OFF");

  canvas.fillSprite(BLACK);

  canvas.pushSprite(
    0,
    0);

  delay(100);

  StickCP2.Power.powerOff();

  while (true) {
    delay(1000);
  }
}

// ============================================================
// setup
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(300);

  // ==========================================================
  // M5StickC Plus2
  // ==========================================================

  auto cfg =
    M5.config();

  StickCP2.begin(cfg);

  StickCP2.Display.setRotation(1);

  StickCP2.Display.setBrightness(
    100);

  // ==========================================================
  // Sprite
  // ==========================================================

  canvas.setColorDepth(16);

  if (
    !canvas.createSprite(
      StickCP2.Display.width(),
      StickCP2.Display.height())) {

    Serial.println(
      "Sprite allocation failed.");

    StickCP2.Display.fillScreen(
      BLACK);

    StickCP2.Display.setTextDatum(
      middle_center);

    StickCP2.Display.setTextColor(
      RED);

    StickCP2.Display.drawString(
      "SPRITE ERROR",
      StickCP2.Display.width() / 2,
      StickCP2.Display.height() / 2);

    while (true) {
      delay(1000);
    }
  }

  canvas.fillSprite(BLACK);

  canvas.pushSprite(
    0,
    0);

  // ==========================================================
  // Grove I2C
  //
  // SDA = GPIO33
  // SCL = GPIO32
  // ==========================================================

  Wire.begin(
    SDA_PIN,
    SCL_PIN,
    100000);

  delay(200);

  // ==========================================================
  // MLX90614
  // ==========================================================

  mlxReady =
    initMLX90614();

  if (
    !mlxReady) {

    drawError(
      "MLX90614 ERROR",
      "Check GY-906");

    return;
  }

  // ==========================================================
  // 起動直後からLIVE
  // ==========================================================

  appState =
    AppState::LIVE;

  filteredObjectTemp =
    NAN;

  resetMedianBuffer();

  // 初期EMAを少し安定させる
  for (
    int i = 0;
    i < 5;
    ++i) {

    readTemperature();

    delay(100);
  }

  drawTemperatureScreen();

  // ==========================================================
  // Button initial state
  // ==========================================================

  StickCP2.update();

  btnAPrev =
    StickCP2.BtnA.isPressed();

  uint32_t now =
    millis();

  lastMeasureMs =
    now;

  lastUserActivityMs =
    now;

  Serial.println(
    "STATE -> LIVE");
}

// ============================================================
// loop
// ============================================================

void loop() {

  StickCP2.update();

  if (
    !mlxReady) {

    delay(100);
    return;
  }

  // ==========================================================
  // このloopの基準時刻
  // ==========================================================

  uint32_t now =
    millis();

  // ==========================================================
  // BtnA physical state
  // ==========================================================

  bool btnANow =
    StickCP2.BtnA.isPressed();

  bool pressedEdge =
    btnANow && !btnAPrev;

  bool releasedEdge =
    !btnANow && btnAPrev;

  btnAPrev =
    btnANow;

  // ==========================================================
  // PRESS
  // ==========================================================

  if (
    pressedEdge) {

    Serial.println(
      "BTN A: PRESS");

    lastUserActivityMs =
      now;

    // --------------------------------------------------------
    // LIVE -> MEASURING
    // --------------------------------------------------------

    if (
      appState == AppState::LIVE) {

      startMeasuring(
        now);
    }

    // --------------------------------------------------------
    // HOLD -> LIVE
    //
    // HOLD中の押下は解除だけ
    // --------------------------------------------------------

    else if (
      appState == AppState::HOLD) {

      enterLive(
        now);
    }
  }

  // ==========================================================
  // RELEASE
  // ==========================================================

  if (
    releasedEdge) {

    Serial.println(
      "BTN A: RELEASE");

    lastUserActivityMs =
      now;

    // --------------------------------------------------------
    // MEASURING -> HOLD
    //
    // RELEASEからLIVEへ直接戻る経路は存在しない
    // --------------------------------------------------------

    if (
      appState == AppState::MEASURING) {

      finishMeasuring(
        now);
    }
  }

  // ==========================================================
  // LIVE / MEASURING
  //
  // リアルタイム温度更新
  // ==========================================================

  if (
    appState == AppState::LIVE || appState == AppState::MEASURING) {

    if (
      now - lastMeasureMs >= MEASURE_INTERVAL_MS) {

      lastMeasureMs =
        now;

      if (
        readTemperature()) {

        drawTemperatureScreen();
      }
    }
  }

  // ==========================================================
  // HOLD
  //
  // 60秒後 -> LIVE
  // ==========================================================

  if (
    appState == AppState::HOLD) {

    // --------------------------------------------------------
    // 重要:
    //
    // finishMeasuring(now) と同じloopで
    // HOLDへ遷移した場合でも、
    //
    // now == holdStartedMs
    //
    // なので差は必ず0から始まる。
    // --------------------------------------------------------

    if (
      now - holdStartedMs >= HOLD_DURATION_MS) {

      Serial.println(
        "HOLD timeout");

      enterLive(
        now);
    }
  }

  // ==========================================================
  // Auto Power Off
  //
  // 最後のBtnA操作から300秒
  // ==========================================================

  if (
    now - lastUserActivityMs >= AUTO_POWER_OFF_MS) {

    powerOffDevice();
  }

  delay(5);
}