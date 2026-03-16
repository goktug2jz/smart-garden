/*
 * =========================================================
 *  Akıllı Bahçe Sulama Vanası Kontrolcüsü
 *  Donanım : NodeMCU ESP8266 + MG996R Servo (D5 pini)
 *  Özellik  : Web arayüzü üzerinden Manuel Açık / Kapalı
 *             ve çoklu zaman programlı Otomatik mod
 *
 *  Gerekli Kütüphaneler (Arduino Library Manager):
 *    - ESP8266WiFi       (ESP8266 core ile gelir)
 *    - ESP8266WebServer  (ESP8266 core ile gelir)
 *    - Servo             (ESP8266 core ile gelir)
 *    - NTPClient         (by Fabrice Weinberg)
 *    - WiFiUdp           (ESP8266 core ile gelir)
 * =========================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// ==================== KULLANICI AYARLARI ====================
const char*   WIFI_SSID     = "WIFI_ADINIZ";
const char*   WIFI_PASS     = "WIFI_SIFRENIZ";
const int     UTC_OFFSET_S  = 3 * 3600;   // UTC+3 (Türkiye)
// ============================================================

// Servo ayarları
#define SERVO_PIN    D5   // GPIO14
#define ANGLE_OPEN   90   // Vanayı açan açı (derece)
#define ANGLE_CLOSE   0   // Vanayı kapatan açı (derece)

// Maksimum program sayısı
#define MAX_SCHEDULES 5

// ---- Nesne tanımlamaları ----
Servo             valveServo;
ESP8266WebServer  server(80);
WiFiUDP           ntpUDP;
NTPClient         timeClient(ntpUDP, "pool.ntp.org", UTC_OFFSET_S, 60000);

// ---- Çalışma modu ----
enum Mode { MANUAL_OPEN, MANUAL_CLOSED, AUTO_MODE };
Mode currentMode = MANUAL_CLOSED;
bool valveOpen   = false;

// ---- Zaman programı yapısı ----
struct Schedule {
    bool     enabled;
    uint8_t  hour;
    uint8_t  minute;
    uint16_t durationMin;  // Vananın açık kalacağı süre (dakika)
};

Schedule schedules[MAX_SCHEDULES] = {
    {true,  8, 0,  10},   // Sabah 08:00 → 10 dakika
    {true,  19, 0, 10},   // Akşam 19:00 → 10 dakika
    {false, 0,  0,  5},
    {false, 0,  0,  5},
    {false, 0,  0,  5}
};

// ---- Otomatik mod iç değişkenleri ----
bool     autoActive      = false;
uint32_t autoStartMs     = 0;
uint32_t autoDurMs       = 0;
uint32_t lastScheduleChk = 0;

// =========================================================
//  Servo yardımcı fonksiyonları
// =========================================================
void openValve() {
    valveServo.write(ANGLE_OPEN);
    valveOpen = true;
    Serial.println("[VANA] Açıldı.");
}

void closeValve() {
    valveServo.write(ANGLE_CLOSE);
    valveOpen = false;
    Serial.println("[VANA] Kapatıldı.");
}

// =========================================================
//  HTML Sayfa Oluşturucu
// =========================================================
String buildPage() {
    timeClient.update();

    // HH:MM
    String timeStr = timeClient.getFormattedTime().substring(0, 5);

    // Durum rozeti
    String badge     = valveOpen
        ? "<span class='badge open'>&#128167; A&#199;IK</span>"
        : "<span class='badge closed'>&#128274; KAPALI</span>";

    // Mod adı
    String modeName;
    String activePanel;
    if      (currentMode == MANUAL_OPEN)   { modeName = "Manuel A&#231;&#305;k";  activePanel = "open";  }
    else if (currentMode == MANUAL_CLOSED) { modeName = "Manuel Kapal&#305;"; activePanel = "close"; }
    else                                   { modeName = "Otomatik";           activePanel = "auto";  }

    // Sekme aktif sınıfları
    String clsOpen  = (currentMode == MANUAL_OPEN)   ? "tab active-open"  : "tab";
    String clsClose = (currentMode == MANUAL_CLOSED) ? "tab active-close" : "tab";
    String clsAuto  = (currentMode == AUTO_MODE)     ? "tab active-auto"  : "tab";

    // Kalan süre etiketi (auto modda)
    String remaining = "";
    if (currentMode == AUTO_MODE && autoActive) {
        uint32_t secLeft = (autoDurMs - (millis() - autoStartMs)) / 1000;
        remaining = "<p class='info'>&#9203; Kapanmaya kalan: <b>" + String(secLeft) + " sn</b></p>";
    }

    String html = R"rawliteral(<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ak&#305;ll&#305; Bah&#231;e Vanas&#305;</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Segoe UI',sans-serif;background:#e8f5e9;min-height:100vh;padding:20px}
  .wrap{max-width:480px;margin:0 auto}
  h1{color:#2e7d32;text-align:center;margin-bottom:20px;font-size:1.45rem}
  .card{background:#fff;border-radius:16px;padding:20px;margin-bottom:14px;box-shadow:0 2px 14px rgba(0,0,0,.09)}
  .status-row{display:flex;justify-content:space-between;align-items:center}
  .badge{padding:7px 18px;border-radius:20px;font-weight:700;font-size:.95rem}
  .badge.open{background:#c8e6c9;color:#1b5e20}
  .badge.closed{background:#ffcdd2;color:#b71c1c}
  .time-lbl{font-size:1.1rem;color:#555}
  .mode-lbl{font-size:.88rem;color:#888;margin-top:5px}
  .sec-title{font-weight:600;color:#333;margin-bottom:12px}
  .tabs{display:flex;gap:8px;margin-bottom:14px}
  .tab{flex:1;padding:10px 4px;border:2px solid #ddd;border-radius:10px;text-align:center;
       cursor:pointer;font-weight:600;font-size:.88rem;color:#555;transition:all .15s}
  .active-open{border-color:#4caf50;background:#e8f5e9;color:#2e7d32}
  .active-close{border-color:#f44336;background:#ffebee;color:#c62828}
  .active-auto{border-color:#1976d2;background:#e3f2fd;color:#0d47a1}
  .panel{display:none}
  .panel.visible{display:block}
  .btn{display:block;width:100%;padding:13px;border:none;border-radius:10px;
       font-size:1rem;font-weight:700;cursor:pointer;margin-top:4px;transition:opacity .2s}
  .btn:hover{opacity:.85}
  .btn-open{background:#4caf50;color:#fff}
  .btn-close{background:#f44336;color:#fff}
  .btn-save{background:#1976d2;color:#fff;margin-top:14px}
  .sch-item{background:#f5f5f5;border-radius:10px;padding:11px 12px;margin-bottom:9px;
            display:flex;align-items:center;gap:8px;flex-wrap:wrap}
  .sch-item label{font-size:.83rem;color:#555}
  .sch-item input[type=time]{border:1px solid #ccc;border-radius:6px;padding:5px 7px;font-size:.9rem;width:100px}
  .sch-item input[type=number]{border:1px solid #ccc;border-radius:6px;padding:5px 7px;font-size:.9rem;width:68px}
  .sch-item input[type=checkbox]{width:17px;height:17px;cursor:pointer;accent-color:#1976d2}
  .info{font-size:.88rem;color:#e65100;margin-top:6px}
  .footer{text-align:center;color:#aaa;font-size:.78rem;margin-top:10px}
</style>
</head>
<body>
<div class="wrap">
  <h1>&#127807; Ak&#305;ll&#305; Bah&#231;e Vanas&#305;</h1>

  <!-- Durum kartı -->
  <div class="card">
    <div class="status-row">
      <div>
        <div class="time-lbl">&#128336; )rawliteral";

    html += timeStr;
    html += R"rawliteral(</div>
        <div class="mode-lbl">Mod: )rawliteral";
    html += modeName;
    html += R"rawliteral(</div>
      </div>
      )rawliteral";
    html += badge;
    html += remaining;
    html += R"rawliteral(
    </div>
  </div>

  <!-- Kontrol kartı -->
  <div class="card">
    <div class="sec-title">Vana Kontrol&#252;</div>

    <!-- Sekmeler -->
    <div class="tabs">
      <div class=")rawliteral";
    html += clsOpen;
    html += R"rawliteral(" onclick="showPanel('open')">&#128167; A&#231;&#305;k</div>
      <div class=")rawliteral";
    html += clsClose;
    html += R"rawliteral(" onclick="showPanel('close')">&#128274; Kapal&#305;</div>
      <div class=")rawliteral";
    html += clsAuto;
    html += R"rawliteral(" onclick="showPanel('auto')">&#9201; Otomatik</div>
    </div>

    <!-- Manuel Aç -->
    <div id="panel-open" class="panel)rawliteral";
    html += (activePanel == "open" ? " visible" : "");
    html += R"rawliteral(">
      <form action="/open" method="POST">
        <button class="btn btn-open" type="submit">&#128167; Vanayi A&#231;</button>
      </form>
    </div>

    <!-- Manuel Kapat -->
    <div id="panel-close" class="panel)rawliteral";
    html += (activePanel == "close" ? " visible" : "");
    html += R"rawliteral(">
      <form action="/close" method="POST">
        <button class="btn btn-close" type="submit">&#128274; Vanayi Kapat</button>
      </form>
    </div>

    <!-- Otomatik Mod -->
    <div id="panel-auto" class="panel)rawliteral";
    html += (activePanel == "auto" ? " visible" : "");
    html += R"rawliteral(">
      <form action="/auto" method="POST">
        <div class="sec-title" style="margin-bottom:10px">&#128197; Sulama Programlar&#305;</div>
)rawliteral";

    // Dinamik program satırları
    for (int i = 0; i < MAX_SCHEDULES; i++) {
        char hh[3], mm[3];
        sprintf(hh, "%02d", schedules[i].hour);
        sprintf(mm, "%02d", schedules[i].minute);

        html += "<div class='sch-item'>";
        html += "<input type='checkbox' name='en" + String(i) + "' value='1'";
        if (schedules[i].enabled) html += " checked";
        html += " title='Aktif/Pasif'>";
        html += "<label>Saat:</label>";
        html += "<input type='time' name='time" + String(i) + "' value='"
                + String(hh) + ":" + String(mm) + "'>";
        html += "<label>S&#252;re&nbsp;(dk):</label>";
        html += "<input type='number' name='dur" + String(i)
                + "' min='1' max='120' value='" + String(schedules[i].durationMin) + "'>";
        html += "</div>";
    }

    html += R"rawliteral(
        <button class="btn btn-save" type="submit">&#128190; Kaydet &amp; Otomati&#287;i Ba&#351;lat</button>
      </form>
    </div>
  </div>

  <div class="footer">Sayfa 30 saniyede bir yenilenir</div>
</div>

<script>
function showPanel(p){
  ['open','close','auto'].forEach(function(x){
    document.getElementById('panel-'+x).classList.remove('visible');
  });
  document.getElementById('panel-'+p).classList.add('visible');
}
setTimeout(function(){ location.reload(); }, 30000);
</script>
</body>
</html>)rawliteral";

    return html;
}

// =========================================================
//  HTTP İşleyiciler
// =========================================================
void handleRoot() {
    server.send(200, "text/html; charset=utf-8", buildPage());
}

void handleOpen() {
    currentMode = MANUAL_OPEN;
    autoActive  = false;
    openValve();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

void handleClose() {
    currentMode = MANUAL_CLOSED;
    autoActive  = false;
    closeValve();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

void handleAuto() {
    for (int i = 0; i < MAX_SCHEDULES; i++) {
        schedules[i].enabled = server.hasArg("en" + String(i));

        String t = server.arg("time" + String(i));
        if (t.length() >= 5) {
            schedules[i].hour   = (uint8_t)t.substring(0, 2).toInt();
            schedules[i].minute = (uint8_t)t.substring(3, 5).toInt();
        }

        String d = server.arg("dur" + String(i));
        if (d.length() > 0) {
            int val = d.toInt();
            schedules[i].durationMin = (uint16_t)constrain(val, 1, 120);
        }
    }

    currentMode = AUTO_MODE;
    autoActive  = false;
    closeValve();

    Serial.println("[AUTO] Program guncellendi:");
    for (int i = 0; i < MAX_SCHEDULES; i++) {
        if (schedules[i].enabled) {
            Serial.printf("  Slot %d: %02d:%02d → %d dk\n",
                i, schedules[i].hour, schedules[i].minute, schedules[i].durationMin);
        }
    }

    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

// =========================================================
//  Otomatik Zamanlama Kontrolü
// =========================================================
void checkAutoSchedule() {
    if (currentMode != AUTO_MODE) return;

    uint32_t now = millis();

    // Açık kalma süresi doldu mu?
    if (autoActive && (now - autoStartMs >= autoDurMs)) {
        closeValve();
        autoActive = false;
        Serial.println("[AUTO] Sure doldu, vana kapatildi.");
    }

    // Her 20 saniyede bir programı kontrol et
    if (!autoActive && (now - lastScheduleChk >= 20000UL)) {
        lastScheduleChk = now;
        timeClient.update();

        int h = timeClient.getHours();
        int m = timeClient.getMinutes();

        for (int i = 0; i < MAX_SCHEDULES; i++) {
            if (schedules[i].enabled &&
                schedules[i].hour   == h &&
                schedules[i].minute == m)
            {
                Serial.printf("[AUTO] Slot %d tetiklendi: %02d:%02d\n", i, h, m);
                openValve();
                autoActive   = true;
                autoStartMs  = now;
                autoDurMs    = (uint32_t)schedules[i].durationMin * 60000UL;
                break;
            }
        }
    }
}

// =========================================================
//  Setup
// =========================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n[BASLIYOR] Akilli Bahce Vanasi");

    // Servo başlat, vana kapalı
    valveServo.attach(SERVO_PIN);
    closeValve();

    // WiFi bağlantısı
    Serial.printf("WiFi'ye baglaniliyor: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("[WiFi] Baglandi! IP: ");
    Serial.println(WiFi.localIP());

    // NTP
    timeClient.begin();
    timeClient.update();
    Serial.print("[NTP] Saat: ");
    Serial.println(timeClient.getFormattedTime());

    // Web sunucusu rotaları
    server.on("/",      HTTP_GET,  handleRoot);
    server.on("/open",  HTTP_POST, handleOpen);
    server.on("/close", HTTP_POST, handleClose);
    server.on("/auto",  HTTP_POST, handleAuto);
    server.begin();
    Serial.println("[HTTP] Web sunucu port 80'de basladi.");
    Serial.println("Tarayicida arayuzu acin: http://" + WiFi.localIP().toString());
}

// =========================================================
//  Loop
// =========================================================
void loop() {
    server.handleClient();
    checkAutoSchedule();
}
