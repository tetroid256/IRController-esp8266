#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUDP.h>
#include <IRsend.h>

#include "config.h"

static constexpr unsigned int kLocalUdpPort = 5001;
static constexpr uint16_t     kIrLedPin     = 4; // GPIO4

WiFiUDP udp;
IRsend  irsend(kIrLedPin);

// 発射用バッファ（最大512要素 = 1024バイト）※グローバルに置くことでスタック破壊を完全防止
uint16_t irBuffer[512]; 

static void connectWiFi() {
  Serial.print(F("Connecting to "));
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println(F("\nWi-Fi Connected."));
  Serial.print(F("IP: "));
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(9600);
  delay(10);
  Serial.println(F("\n--- Ultimate IR Cannon (Binary Edition) ---"));

  irsend.begin();
  connectWiFi();
  udp.begin(kLocalUdpPort);
  Serial.print(F("Listening on UDP port "));
  Serial.println(kLocalUdpPort);
}

void loop() {
  // 1. パケットが届いたか確認
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;

  // 2. パケットサイズが異常じゃないか、かつ2の倍数（uint16_tは2バイトだから）かチェック
  if (packetSize > 0 && packetSize <= sizeof(irBuffer) && packetSize % 2 == 0) {
    
    // 3. 飛んできたバイナリデータを、irBuffer（配列）に直接叩き込む！
    udp.read((char*)irBuffer, packetSize);
    
    // 4. 何個の数字が入っていたか計算（バイト数 ÷ 2）
    int dataLength = packetSize / 2;

    Serial.print(F("[AC] 弾丸を装填完了！ 発射数: "));
    Serial.println(dataLength);

    // 5. 発射！
    irsend.sendRaw(irBuffer, dataLength, 38);
    
  } else {
    // もし変なデータが来たら無視して捨てる
    Serial.print(F("[ERR] 謎のパケットを受信: "));
    Serial.println(packetSize);
    udp.flush();
  }
}