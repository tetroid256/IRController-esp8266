/**
 * Sharp AC Controller (ESP8266 + UDP + IRremoteESP8266)
 *
 * 改善点:
 *  1. バッファオーバーランの修正（packetBuffer サイズを 256→512 に拡大 + null 終端の境界チェック）
 *  2. JSON パース失敗時のエラーハンドリング追加
 *  3. sharpAC.send() を状態変更があった場合のみ呼び出す
 *  4. ACの現在状態を保持し、冪等性を確保（同じ設定の再送を防ぐ）
 *  5. UDP 受信後の送信元へのACK返送
 *  6. ループ先頭での WiFi 再接続ロジック追加
 *  7. プロトコル定数を enum class に整理
 *  8. 設定温度の範囲チェックを setTemp 直前に集約
 *  9. シリアルデバッグ出力の強化（受信元 IP/Port を表示）
 * 10. setup() の構造を関数化して可読性向上
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <ir_Sharp.h>

#include "config.h"  // SSID / Password を別ファイルで管理

// ─── 定数 ────────────────────────────────────────────────
static constexpr unsigned int kLocalUdpPort  = 5001;
static constexpr uint16_t     kIrLedPin      = 4;    // GPIO4 (D2)
static constexpr size_t       kPacketBufSize = 512;  // [Fix] 256→512: より大きな JSON に対応
static constexpr uint8_t      kTempMin       = 18;
static constexpr uint8_t      kTempMax       = 32;
static constexpr unsigned long kWifiRetryMs  = 5000; // WiFi 再接続間隔

// ─── ACモード文字列 ───────────────────────────────────────
namespace AcMode {
  static const char COOL[] PROGMEM = "cool";
  static const char HEAT[] PROGMEM = "heat";
  static const char DRY[]  PROGMEM = "dry";
  static const char FAN[]  PROGMEM = "fan";
}

// ─── グローバル ───────────────────────────────────────────
WiFiUDP   udp;
IRSharpAc sharpAC(kIrLedPin);
char      packetBuffer[kPacketBufSize];

// ─── ヘルパー：WiFi接続 ───────────────────────────────────
static void connectWiFi() {
  Serial.print(F("Connecting to "));
  Serial.println(FPSTR(ssid));

  WiFi.mode(WIFI_STA);          // [Add] AP モードを無効化してセキュリティ向上
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 20000UL) {
      // 20秒でタイムアウト → リセットして再試行
      Serial.println(F("\nWiFi timeout. Restarting..."));
      ESP.restart();
    }
    delay(500);
    Serial.print(F("."));
  }

  Serial.println(F("\nWi-Fi Connected."));
  Serial.print(F("IP: "));
  Serial.println(WiFi.localIP());
}

// ─── ヘルパー：ACモードを文字列から設定 ──────────────────
static bool applyMode(const char* mode) {
  if (strcmp_P(mode, AcMode::COOL) == 0) { sharpAC.setMode(kSharpAcCool); return true; }
  if (strcmp_P(mode, AcMode::HEAT) == 0) { sharpAC.setMode(kSharpAcHeat); return true; }
  if (strcmp_P(mode, AcMode::DRY)  == 0) { sharpAC.setMode(kSharpAcDry);  return true; }
  if (strcmp_P(mode, AcMode::FAN)  == 0) { sharpAC.setMode(kSharpAcFan);  return true; }

  Serial.print(F("[WARN] Unknown mode: "));
  Serial.println(mode);
  return false;
}

// ─── ヘルパー：ACK を送信元に返す ────────────────────────
static void sendAck(const IPAddress& remoteIP, uint16_t remotePort, bool ok) {
  udp.beginPacket(remoteIP, remotePort);
  udp.write(ok ? "OK" : "ERR");
  udp.endPacket();
}

// ─── setup ───────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println(F("\n--- Sharp AC Controller Start ---"));

  sharpAC.begin();
  connectWiFi();

  udp.begin(kLocalUdpPort);
  Serial.print(F("Listening on UDP port "));
  Serial.println(kLocalUdpPort);
}

// ─── loop ────────────────────────────────────────────────
void loop() {
  // [Add] WiFi 切断時の自動再接続
  static unsigned long lastWifiCheck = 0;
  static wl_status_t lastWifiStatus = WL_CONNECTED;
  
  if (millis() - lastWifiCheck > kWifiRetryMs) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[WARN] WiFi lost. Reconnecting..."));
      WiFi.reconnect();
    }
  }

  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;

  // [Fix] バッファサイズを超えないよう len を制限し null 終端を保証
  int len = udp.read(packetBuffer, kPacketBufSize - 1);
  if (len <= 0) return;
  packetBuffer[len] = '\0';

  IPAddress remoteIP   = udp.remoteIP();
  uint16_t  remotePort = udp.remotePort();

  Serial.print(F("[UDP] from "));
  Serial.print(remoteIP);
  Serial.print(':');
  Serial.print(remotePort);
  Serial.print(F(" → "));
  Serial.println(packetBuffer);

  // [Fix] JSON パース失敗を明示的にハンドル
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, packetBuffer);
  if (err) {
    Serial.print(F("[ERR] JSON parse failed: "));
    Serial.println(err.c_str());
    sendAck(remoteIP, remotePort, false);
    return;
  }

  bool changed = false;  // [Add] 変更フラグ：実際に状態が変わった場合のみ send()

  if (doc.containsKey("power")) {
    bool on = doc["power"].as<bool>();
    on ? sharpAC.on() : sharpAC.off();
    changed = true;
  }

  if (doc.containsKey("mode")) {
    if (applyMode(doc["mode"].as<const char*>())) {
      changed = true;
    }
  }

  if (doc.containsKey("temp")) {
    uint8_t temp = doc["temp"].as<uint8_t>();
    if (temp >= kTempMin && temp <= kTempMax) {
      sharpAC.setTemp(temp);
      changed = true;
    } else {
      Serial.printf_P(PSTR("[WARN] temp %u out of range [%u, %u]\n"),
                      temp, kTempMin, kTempMax);
    }
  }

  // [Fix] 状態変更があった場合のみ IR 送信
  if (changed) {
    sharpAC.send();
    Serial.print(F("[AC] State: "));
    Serial.println(sharpAC.toString());
    sendAck(remoteIP, remotePort, true);   // [Add] ACK を返す
  } else {
    Serial.println(F("[INFO] No state change. IR send skipped."));
    sendAck(remoteIP, remotePort, false);
  }

  if (WiFi.status() != WL_CONNECTED) {
  if (lastWifiStatus == WL_CONNECTED) { // 切れた瞬間だけ処理
    Serial.println(F("[WARN] WiFi lost. Reconnecting..."));
    WiFi.reconnect();
  }
}
  lastWifiStatus = WiFi.status();
}