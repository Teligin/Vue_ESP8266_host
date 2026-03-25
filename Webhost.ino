#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <DNSServer.h>

extern "C" {
  #include "user_interface.h"
}

const char* ssid = "PS4_VUE_AFTER_FREE";
AsyncWebServer server(80);
WiFiUDP ntpServer;
DNSServer dnsServer;

struct TimeStore { uint32_t unix; uint32_t magic; } savedTime;
uint32_t current_unix = 1710547200;
unsigned long last_ms = 0;

uint32_t getTime() { return (last_ms == 0) ? current_unix : current_unix + (millis() - last_ms) / 1000; }

void saveTime(uint32_t t) {
  savedTime.unix = t; savedTime.magic = 0xDEADBEEF;
  EEPROM.put(0, savedTime); EEPROM.commit();
}

String getClients() {
  String out = "";
  struct station_info *stat = wifi_softap_get_station_info();
  while (stat != NULL) {
    IPAddress ip = &stat->ip;
    out += "<option value='" + ip.toString() + "'>" + ip.toString() + "</option>";
    stat = STAILQ_NEXT(stat, next);
  }
  return out;
}

void handleNTP() {
  int cb = ntpServer.parsePacket();
  if (cb) {
    byte pBuf[48]; 
    ntpServer.read(pBuf, 48);
    byte resp[48]; 
    memset(resp, 0, 48);

    resp[0] = 0b00100100; // LI=0, VN=4, Mode=4
    resp[1] = 1;          // Stratum
    resp[2] = 6; 
    resp[3] = 0xFA;
    
    // Root Delay & Dispersion
    resp[5] = 0x01; resp[9] = 0x01; 
    memcpy(&resp[12], "GPS ", 4);

    uint32_t t = getTime() + 2208988800UL;
    
    // Reference/Transmit Timestamp
    resp[16] = (t >> 24) & 0xFF; resp[17] = (t >> 16) & 0xFF;
    resp[18] = (t >> 8) & 0xFF;  resp[19] = t & 0xFF;
    resp[40] = resp[16]; resp[41] = resp[17]; resp[42] = resp[18]; resp[43] = resp[19];

    // Origin Timestamp (обязательно для PS4/GoldHEN)
    for(int i=0; i<8; i++) resp[24+i] = pBuf[24+i];

    ntpServer.beginPacket(ntpServer.remoteIP(), ntpServer.remotePort());
    ntpServer.write(resp, 48); 
    ntpServer.endPacket();
    Serial.println("[NTP] Sent!");
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  EEPROM.get(0, savedTime);
  if (savedTime.magic == 0xDEADBEEF) { current_unix = savedTime.unix; last_ms = millis(); }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid);

  dnsServer.start(53, "*", IPAddress(192,168,4,1));
  ntpServer.begin(123);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String clients = getClients();
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;padding:20px;background:#f0f0f0} .box{background:#fff;padding:15px;margin-bottom:15px;border-radius:8px} button{padding:10px;width:100%;margin-top:10px;cursor:pointer;background:#2196F3;color:#fff;border:none} input,select{width:100%;padding:8px;margin:5px 0}</style></head><body>"
    "<h2>PS4 Web Panel</h2>"
    "<div class='box'><b>Time:</b> <span id='t'>...</span><br><button onclick='st()'>Sync Browser</button></div>"
    "<div class='box'><b>Payload:</b><br>"
    "List: <select id='ipSelect' onchange='document.getElementById(\"ip\").value=this.value'>" + clients + "<option value=''>Custom IP...</option></select>"
    "Target IP: <input type='text' id='ip' value='192.168.4.100'>"
    "Port: <input type='number' id='port' value='9090'>"
    "File: <input type='file' id='f'><br>"
    "<button onclick='sendPl()' style='background:#4CAF50'>SEND TO PS4</button>"
    "<p id='stat' style='color:blue;font-size:12px'></p></div>"
    "<script>"
    "function st(){fetch('/set?t='+Math.floor(Date.now()/1000)).then(()=>location.reload());}"
    "fetch('/get').then(r=>r.text()).then(t=>{if(t>0)document.getElementById('t').innerText=new Date(t*1000).toLocaleString();});"
    "function sendPl(){"
    "  const fIn = document.getElementById('f'); if(fIn.files.length===0) return alert('Select file!');"
    "  const ip = document.getElementById('ip').value; const port = document.getElementById('port').value;"
    "  const log = document.getElementById('stat'); log.innerText = 'Sending to '+ip+'...';"
    "  const reader = new FileReader();"
    "  reader.onload = function(e){"
    "    const xhr = new XMLHttpRequest();"
    "    xhr.open('POST', 'http://' + ip + ':' + port + '/', true);"
    "    xhr.send(e.target.result);"
    "    xhr.onload = () => { log.innerText = 'Done!'; };"
    "    xhr.onerror = () => { log.innerText = 'Sent (Check PS4)'; };"
    "  }; reader.readAsArrayBuffer(fIn.files[0]);"
    "}</script></body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("t")){ current_unix = request->getParam("t")->value().toInt(); last_ms = millis(); saveTime(current_unix); }
    request->send(200);
  });

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", String(getTime())); });
  server.onNotFound([](AsyncWebServerRequest *request){ request->redirect("/"); });
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  handleNTP();
  yield();
}
