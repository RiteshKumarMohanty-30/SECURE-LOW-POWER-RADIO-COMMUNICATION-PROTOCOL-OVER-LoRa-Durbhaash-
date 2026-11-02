/*
============================================================
SECURE LOW-POWER RADIO COMMUNICATION PROTOCOL OVER LoRa
HELTEC WIFI LORA 32 V3 - NODE 1
============================================================
NODE 1 -> NODE 2

v4 changes:
- New web UI (chat bubbles, delivery states, emergency overlay,
  presets, signal bars, counters, resend, persistent history)
- Non-blocking send (state machine) - web server stays responsive
- Adaptive ACK timeout + random backoff
- More retries for emergency messages
- Sliding-window replay protection (accepts out-of-order IDs)
- /send and /clear are POST requests
============================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <Preferences.h>

#include "mbedtls/gcm.h"
#include "mbedtls/base64.h"

// ============================================================
// NODE CONFIG
// ============================================================

#define NODE_ID 1
#define DEST_ID 2

const char* AP_NAME = "Durbhaash_Node1";
const char* AP_PASSWORD = "LoRa12345";

// ============================================================
// LORA CONFIG  (must be IDENTICAL on Node 1 and Node 2)
// ============================================================
// 433~510 MHz board variant: use 433.5 (India license-exempt 433-434.79 MHz).

#define LORA_FREQUENCY 433.5
#define LORA_BANDWIDTH 125.0
#define LORA_SF 9
#define LORA_CR 5
#define LORA_SYNCWORD 0x12
#define LORA_POWER 10
#define LORA_PREAMBLE 8

// ============================================================
// HELTEC WIFI LORA 32 V3 PINS
// ============================================================

#define LORA_CS 8
#define LORA_DIO1 14
#define LORA_RST 12
#define LORA_BUSY 13

#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

#define VEXT_PIN 36

// ============================================================
// RADIO / OLED / SERVER
// ============================================================

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

WebServer server(80);
Preferences preferences;

// ============================================================
// AES KEY (must be IDENTICAL on both nodes)
// ============================================================

const uint8_t AES_KEY[16] = {
  0x42, 0x71, 0x19, 0xA4,
  0xD3, 0x58, 0x2C, 0xF0,
  0x91, 0xE6, 0x37, 0x5B,
  0xAC, 0x08, 0x74, 0xCD
};

// ============================================================
// PROTOCOL
// ============================================================

#define PROTOCOL_VERSION "V3"

#define DOMAIN_MESSAGE 0x01
#define DOMAIN_ACK 0x02

#define MAX_TEXT_LENGTH 100

#define RETRIES_NORMAL 3
#define RETRIES_EMERGENCY 5

// ============================================================
// STATE
// ============================================================

volatile bool packetReceived = false;

uint64_t txCounter = 0;
uint64_t rxHighest = 0;
uint32_t rxWindow = 0xFFFFFFFF;   // bit i set => (rxHighest - i) already seen

// Current packet RSSI/SNR (captured right after readData)
int curRSSI = 0;
float curSNR = 0.0;

// Last authenticated packet info
int lastRSSI = 0;
float lastSNR = 0.0;
unsigned long lastRxMillis = 0;

String lastMessage = "";
String lastMessageType = "";
String lastMessageSource = "";

unsigned long lastDisplayUpdate = 0;

// Statistics
uint32_t statSent = 0;
uint32_t statDelivered = 0;
uint32_t statFailed = 0;
uint32_t statReceived = 0;

// TX state machine
bool txActive = false;
bool ackReceived = false;
String txPacket = "";
String txText = "";
char txType = 'N';
uint64_t txID = 0;
int txAttempt = 0;
int txMaxAttempts = RETRIES_NORMAL;
unsigned long txLastTime = 0;
unsigned long txTimeout = 0;
int ackRSSI = 0;
float ackSNR = 0.0;

// ============================================================
// HISTORY
// ============================================================

#define HISTORY_SIZE 20

#define ST_SENDING 0
#define ST_DELIVERED 1
#define ST_FAILED 2

struct HistoryMessage {
  String direction;
  String type;
  String text;
  uint64_t id;
  int rssi;
  float snr;
  uint8_t status;
  uint32_t ms;
};

HistoryMessage history[HISTORY_SIZE];
int historyCount = 0;
uint32_t historyVersion = 1;

// ============================================================
// WEB PAGE (global PROGMEM raw string)
// ============================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Durbhaash</title>
<meta name="theme-color" content="#070d1a">
<style>
:root{--bg:#070d1a;--panel:#0f1a2e;--panel2:#0b1424;--line:#22365a;--txt:#eaf1ff;--mut:#8ea3c4;--acc:#38bdf8;--acc2:#2563eb;--ok:#4ade80;--bad:#f87171;--warn:#facc15;}
*{box-sizing:border-box;}
body{margin:0;font-family:system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;background:radial-gradient(1200px 500px at 50% -120px,#12284d 0%,var(--bg) 60%);background-color:var(--bg);color:var(--txt);min-height:100vh;}
.hidden{display:none !important;}
.top{position:sticky;top:0;z-index:20;padding:14px 18px;background:rgba(10,18,34,.88);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);border-bottom:1px solid var(--line);}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;max-width:760px;margin:auto;}
.brand{display:flex;align-items:center;gap:12px;}
.logo{flex:none;filter:drop-shadow(0 0 8px rgba(56,189,248,.45));}
.top h1{margin:0;font-size:22px;letter-spacing:.2em;font-weight:800;background:linear-gradient(90deg,#fff,#7dd3fc);-webkit-background-clip:text;background-clip:text;color:transparent;}
.badge{display:inline-block;margin-left:8px;padding:2px 9px;border-radius:999px;font-size:11px;letter-spacing:.08em;font-weight:700;color:#06101f;background:var(--acc);vertical-align:middle;-webkit-text-fill-color:#06101f;}
.top p{margin:4px 0 0;color:var(--mut);font-size:12px;letter-spacing:.03em;}
.sig{display:flex;align-items:flex-end;gap:3px;height:24px;}
.sig i{display:block;width:6px;background:#26395c;border-radius:2px;transition:background .3s;}
.sig i:nth-child(1){height:7px;}
.sig i:nth-child(2){height:12px;}
.sig i:nth-child(3){height:18px;}
.sig i:nth-child(4){height:24px;}
.sig i.on{background:var(--ok);box-shadow:0 0 6px rgba(74,222,128,.5);}
.pill{max-width:760px;margin:10px auto 0;display:table;padding:5px 13px;border-radius:999px;font-size:12px;background:#15233d;color:var(--mut);border:1px solid var(--line);}
.pill.q4,.pill.q3{background:#0f2e1d;color:var(--ok);border-color:#1d5a37;}
.pill.q2{background:#33290c;color:var(--warn);border-color:#5e4b13;}
.pill.q1{background:#351717;color:var(--bad);border-color:#6a2a2a;}
.pill.stale{background:#232323;color:#aaa;border-color:#3a3a3a;}
.wrap{max-width:760px;margin:auto;padding:16px;}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:16px;}
.stat{background:linear-gradient(160deg,var(--panel),var(--panel2));border:1px solid var(--line);border-radius:14px;padding:11px 13px;}
.stat small{color:var(--mut);font-size:10px;letter-spacing:.1em;text-transform:uppercase;}
.stat strong{display:block;margin-top:4px;font-size:17px;}
.card{background:linear-gradient(160deg,var(--panel),var(--panel2));border:1px solid var(--line);border-radius:18px;padding:16px;margin-bottom:16px;box-shadow:0 8px 24px rgba(0,0,0,.25);}
.cardhead{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;}
.cardhead h3{margin:0;font-size:15px;letter-spacing:.04em;}
.small{background:#172846;color:#c7d6ee;border:1px solid var(--line);border-radius:9px;padding:6px 11px;font-size:12px;font-weight:normal;flex:none;cursor:pointer;margin-left:4px;}
.small:active{transform:scale(.96);}
.chat{height:360px;overflow-y:auto;padding:6px;background:rgba(5,10,20,.45);border-radius:12px;scroll-behavior:smooth;}
.empty{color:#667a9a;text-align:center;margin-top:70px;font-size:14px;}
.msg{max-width:84%;padding:10px 13px;border-radius:16px;margin-bottom:10px;background:#14213a;border:1px solid #1f3254;animation:pop .2s ease;}
@keyframes pop{from{opacity:0;transform:translateY(6px);}to{opacity:1;transform:none;}}
.msg.tx{margin-left:auto;background:linear-gradient(135deg,#1d4ed8,#1e3a8a);border-color:#2f5bd6;border-bottom-right-radius:4px;}
.msg.rx{margin-right:auto;border-bottom-left-radius:4px;}
.msg.em{border:2px solid #ef4444;box-shadow:0 0 14px rgba(239,68,68,.35);}
.meta{font-size:11px;color:#a9bbd8;margin-bottom:5px;}
.em .meta{color:#fca5a5;font-weight:bold;}
.text{word-break:break-word;font-size:15px;line-height:1.4;}
.st{display:block;margin-top:6px;font-size:11px;}
.st.ok{color:var(--ok);}
.st.bad{color:var(--bad);}
.st.sending{color:var(--warn);}
.st a{color:#93c5fd;}
.txbar{margin-top:10px;padding:9px 12px;border-radius:10px;background:#33290c;color:var(--warn);font-size:13px;border:1px solid #5e4b13;}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:12px;}
.chip{background:#172846;color:#c7d6ee;border:1px solid var(--line);border-radius:999px;padding:7px 13px;font-size:13px;font-weight:normal;flex:none;cursor:pointer;}
.chip:active{background:#1f3a66;}
textarea{width:100%;min-height:90px;resize:vertical;background:#081120;color:#fff;border:1px solid #2c4468;border-radius:12px;padding:12px;font-size:16px;font-family:inherit;outline:none;}
textarea:focus{border-color:var(--acc);box-shadow:0 0 0 3px rgba(56,189,248,.18);}
.count{text-align:right;color:#667a9a;font-size:12px;margin-top:4px;}
.buttons{display:flex;gap:10px;margin-top:10px;}
.buttons button{flex:1;border:0;border-radius:12px;padding:15px;color:#fff;font-weight:700;letter-spacing:.04em;cursor:pointer;font-size:14px;}
.buttons button:active{transform:scale(.98);}
.buttons button:disabled{opacity:.45;cursor:not-allowed;}
.normal{background:linear-gradient(135deg,#3b82f6,#2563eb);}
.emergency{background:linear-gradient(135deg,#ef4444,#b91c1c);box-shadow:0 0 14px rgba(239,68,68,.35);}
.footer{text-align:center;color:#5d7292;font-size:11px;padding:18px;letter-spacing:.06em;}
.alert{position:fixed;inset:0;z-index:50;display:flex;align-items:center;justify-content:center;padding:20px;background:rgba(127,29,29,0.96);animation:pulse 1s infinite;}
@keyframes pulse{0%{background:rgba(127,29,29,0.96);}50%{background:rgba(185,28,28,0.98);}100%{background:rgba(127,29,29,0.96);}}
.alertbox{width:100%;max-width:440px;background:#1a0b0b;border:2px solid #fca5a5;border-radius:18px;padding:22px;text-align:center;}
.alertbox h2{margin:0 0 4px;font-size:26px;color:#fecaca;}
.alertbox .big{margin:16px 0;font-size:24px;font-weight:bold;word-break:break-word;}
.alertbox .info{color:#fca5a5;font-size:14px;line-height:1.7;}
.alertbox button{margin-top:18px;width:100%;border:0;border-radius:12px;padding:16px;background:#fff;color:#7f1d1d;font-size:17px;font-weight:bold;cursor:pointer;}
@media (max-width:420px){.top h1{font-size:18px;letter-spacing:.14em;}.chat{height:300px;}}
</style>
</head>
<body>

<div class="top">
  <div class="row">
    <div class="brand">
      <svg class="logo" viewBox="0 0 32 36" width="34" height="38" aria-hidden="true">
        <defs><linearGradient id="lg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#38bdf8"/><stop offset="1" stop-color="#2563eb"/></linearGradient></defs>
        <path d="M16 1.5 L29.5 6.5 V18 C29.5 26.5 23.5 32.5 16 34.5 C8.5 32.5 2.5 26.5 2.5 18 V6.5 Z" fill="url(#lg)"/>
        <circle cx="16" cy="14.5" r="4.2" fill="#070d1a"/>
        <rect x="14.4" y="16.5" width="3.2" height="9.5" rx="1.6" fill="#070d1a"/>
      </svg>
      <div>
        <h1>DURBHAASH<span class="badge">NODE <span id="nodeNum">-</span></span></h1>
        <p>Secure emergency radio messaging &bull; AES-128-GCM</p>
      </div>
    </div>
    <div class="sig" id="sig"><i></i><i></i><i></i><i></i></div>
  </div>
  <div class="pill" id="link">Connecting...</div>
</div>

<div class="wrap">

  <div class="grid">
    <div class="stat"><small>RSSI</small><strong id="rssi">--</strong></div>
    <div class="stat"><small>SNR</small><strong id="snr">--</strong></div>
    <div class="stat"><small>Settings</small><strong id="cfg">--</strong></div>
    <div class="stat"><small>Sent</small><strong id="sent">0</strong></div>
    <div class="stat"><small>Delivered</small><strong id="delivered">0</strong></div>
    <div class="stat"><small>Failed</small><strong id="failed">0</strong></div>
  </div>

  <div class="card">
    <div class="cardhead">
      <h3>Messages <span id="rxcount" style="color:#70839f;font-weight:normal;font-size:12px"></span></h3>
      <div>
        <button class="small" id="soundBtn" onclick="toggleSound()">Sound: ON</button>
        <button class="small" onclick="clearAll()">Clear</button>
      </div>
    </div>
    <div class="chat" id="chat"><div class="empty">No messages yet</div></div>
    <div class="txbar hidden" id="txbar"></div>
  </div>

  <div class="card">
    <div class="chips" id="chips"></div>
    <textarea id="msg" maxlength="100" placeholder="Type a message (max 100 characters)..." oninput="updateCount()"></textarea>
    <div class="count"><span id="cnt">0</span>/100</div>
    <div class="buttons">
      <button class="normal" id="btnN" onclick="sendBtn('N')">SEND NORMAL</button>
      <button class="emergency" id="btnE" onclick="sendBtn('E')">SEND EMERGENCY</button>
    </div>
  </div>

</div>

<div class="footer">DURBHAASH &bull; SECURE LOW-POWER RADIO COMMUNICATION &bull; ESP32-S3 &bull; SX1262</div>

<div class="alert hidden" id="alert">
  <div class="alertbox">
    <h2>&#9888; EMERGENCY</h2>
    <div class="info">Priority: CRITICAL</div>
    <div class="big" id="alText"></div>
    <div class="info" id="alInfo"></div>
    <button onclick="dismissAlert()">ACKNOWLEDGE</button>
  </div>
</div>

<script>
var state = {ver:-1, hist:[], node:0, dest:0, busy:false};
var soundOn = true;
var audioCtx = null;
var alertTimer = null;
var currentAlertId = 0;

var PRESETS = ["Need help","I am safe","Medical emergency","Fire!","Need pickup","Low battery"];

function $(id){ return document.getElementById(id); }

function esc(v){
  return String(v)
    .replace(/&/g,"&amp;")
    .replace(/</g,"&lt;")
    .replace(/>/g,"&gt;")
    .replace(/"/g,"&quot;")
    .replace(/'/g,"&#039;");
}

function two(n){ return (n < 10 ? "0" : "") + n; }

function fmtTime(ms){
  var d = new Date(ms);
  return two(d.getHours()) + ":" + two(d.getMinutes()) + ":" + two(d.getSeconds());
}

function ago(sec){
  if(sec < 0) return "never";
  if(sec < 60) return sec + "s ago";
  if(sec < 3600) return Math.floor(sec/60) + "m ago";
  return Math.floor(sec/3600) + "h ago";
}

/* ---------- sound ---------- */

function ensureAudio(){
  try{
    if(!audioCtx){
      audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    }
    if(audioCtx.state === "suspended"){ audioCtx.resume(); }
  }catch(e){}
}

document.addEventListener("click", ensureAudio);
document.addEventListener("touchstart", ensureAudio);

function beep(){
  if(!soundOn) return;
  try{
    ensureAudio();
    var o = audioCtx.createOscillator();
    var g = audioCtx.createGain();
    o.type = "square";
    o.frequency.value = 880;
    g.gain.value = 0.15;
    o.connect(g);
    g.connect(audioCtx.destination);
    o.start();
    o.stop(audioCtx.currentTime + 0.25);
  }catch(e){}
}

function toggleSound(){
  soundOn = !soundOn;
  $("soundBtn").textContent = "Sound: " + (soundOn ? "ON" : "OFF");
}

/* ---------- emergency overlay ---------- */

function seenKey(){ return "em_seen_" + state.node; }

function getSeen(){
  try{ return parseInt(localStorage.getItem(seenKey()) || "0", 10) || 0; }
  catch(e){ return 0; }
}

function setSeen(id){
  try{ localStorage.setItem(seenKey(), String(id)); }catch(e){}
}

function showAlert(m){
  currentAlertId = m.id;
  $("alText").textContent = m.text;
  $("alInfo").innerHTML =
    "FROM: NODE " + esc(state.dest) + "<br>" +
    "ID: " + esc(m.id) + " &bull; " + fmtTime(m.time) + "<br>" +
    "RSSI " + esc(m.rssi) + " dBm &bull; SNR " + esc(m.snr) + " dB<br>" +
    "&#10003; AUTHENTICATED";
  $("alert").classList.remove("hidden");

  if(alertTimer){ clearInterval(alertTimer); }
  beep();
  if(navigator.vibrate){ navigator.vibrate([400,150,400,150,400]); }
  alertTimer = setInterval(function(){
    beep();
    if(navigator.vibrate){ navigator.vibrate([300,100,300]); }
  }, 1500);
}

function dismissAlert(){
  if(alertTimer){ clearInterval(alertTimer); alertTimer = null; }
  setSeen(currentAlertId);
  $("alert").classList.add("hidden");
}

function checkEmergency(){
  var last = null;
  for(var i = 0; i < state.hist.length; i++){
    var m = state.hist[i];
    if(m.dir === "RX" && m.type === "Emergency"){ last = m; }
  }
  if(!last) return;
  if(last.id > getSeen() && $("alert").classList.contains("hidden")){
    showAlert(last);
  }
}

/* ---------- rendering ---------- */

function bubble(m, idx){
  var em = (m.type === "Emergency");
  var cls = "msg " + (m.dir === "TX" ? "tx" : "rx") + (em ? " em" : "");
  var meta = (em ? "EMERGENCY \u2022 " : "") +
             (m.dir === "TX" ? "You" : "Durbhaash Node " + state.dest) +
             " \u2022 ID " + m.id + " \u2022 " + fmtTime(m.time);
  var foot = "";

  if(m.dir === "TX"){
    if(m.status === 0){
      foot = '<span class="st sending">Sending\u2026</span>';
    }else if(m.status === 1){
      foot = '<span class="st ok">\u2713 Delivered \u2022 ACK ' + esc(m.rssi) + ' dBm</span>';
    }else{
      foot = '<span class="st bad">\u2715 No ACK \u2022 <a href="#" onclick="resend(' + idx + ');return false;">Resend</a></span>';
    }
  }else{
    foot = '<span class="st ok">\u2713 Authenticated \u2022 ' + esc(m.rssi) + ' dBm \u2022 SNR ' + esc(m.snr) + ' dB</span>';
  }

  return '<div class="' + cls + '">' +
         '<div class="meta">' + esc(meta) + '</div>' +
         '<div class="text">' + esc(m.text) + '</div>' +
         foot + '</div>';
}

function renderChat(){
  var chat = $("chat");
  var atBottom = (chat.scrollTop + chat.clientHeight >= chat.scrollHeight - 40);

  if(state.hist.length === 0){
    chat.innerHTML = '<div class="empty">No messages yet</div>';
    return;
  }

  var html = "";
  for(var i = 0; i < state.hist.length; i++){
    html += bubble(state.hist[i], i);
  }
  chat.innerHTML = html;

  if(atBottom){ chat.scrollTop = chat.scrollHeight; }
}

function renderStatus(s){
  state.node = s.node;
  state.dest = s.destination;
  state.busy = s.txBusy;

  $("nodeNum").textContent = s.node;
  $("cfg").textContent = s.frequency + " / SF" + s.sf;
  $("rssi").textContent = (s.lastRxAge < 0) ? "--" : s.rssi + " dBm";
  $("snr").textContent = (s.lastRxAge < 0) ? "--" : s.snr + " dB";
  $("sent").textContent = s.sent;
  $("delivered").textContent = s.delivered;
  $("failed").textContent = s.failed;
  $("rxcount").textContent = "(" + s.received + " received)";

  var bars = $("sig").children;
  for(var i = 0; i < 4; i++){
    bars[i].className = (i < s.quality) ? "on" : "";
  }

  var names = ["", "Poor", "Weak", "Good", "Excellent"];
  var pill = $("link");
  if(s.lastRxAge < 0){
    pill.textContent = "No packets received yet";
    pill.className = "pill";
  }else{
    pill.textContent = "Link: " + names[s.quality] + " \u2022 last packet " + ago(s.lastRxAge);
    pill.className = "pill q" + s.quality + (s.lastRxAge > 300 ? " stale" : "");
  }

  $("btnN").disabled = s.txBusy;
  $("btnE").disabled = s.txBusy;

  var bar = $("txbar");
  if(s.txBusy){
    bar.textContent = "Transmitting \u2022 attempt " + s.txAttempt + "/" + s.txMax + " \u2022 waiting for ACK";
    bar.classList.remove("hidden");
  }else{
    bar.classList.add("hidden");
  }
}

/* ---------- network ---------- */

function loadHistory(ver){
  return fetch("/history")
    .then(function(r){ return r.json(); })
    .then(function(h){
      var now = Date.now();
      for(var i = 0; i < h.length; i++){
        h[i].time = now - h[i].age * 1000;
      }
      state.hist = h;
      state.ver = ver;
      renderChat();
      checkEmergency();
    });
}

function poll(){
  fetch("/status")
    .then(function(r){ return r.json(); })
    .then(function(s){
      renderStatus(s);
      if(s.ver !== state.ver){ return loadHistory(s.ver); }
    })
    .catch(function(){
      $("link").textContent = "Cannot reach node";
      $("link").className = "pill q1";
    })
    .then(function(){
      setTimeout(poll, state.busy ? 500 : 1000);
    });
}

function doSend(text, type, clearBox){
  if(type === "E" && !confirm("Send EMERGENCY message?")){ return; }

  $("btnN").disabled = true;
  $("btnE").disabled = true;

  fetch("/send", {
    method: "POST",
    headers: {"Content-Type": "application/x-www-form-urlencoded"},
    body: "type=" + type + "&message=" + encodeURIComponent(text)
  })
  .then(function(r){ return r.json(); })
  .then(function(d){
    if(!d.success){
      alert(d.message || "Could not send");
    }else if(clearBox){
      $("msg").value = "";
      updateCount();
    }
    state.ver = -1;
  })
  .catch(function(){ alert("Could not reach node"); });
}

function sendBtn(type){
  var text = $("msg").value.trim();
  if(!text){ alert("Enter a message first."); return; }
  if(text.length > 100){ alert("Maximum 100 characters."); return; }
  doSend(text, type, true);
}

function resend(idx){
  var m = state.hist[idx];
  if(!m) return;
  doSend(m.text, m.type === "Emergency" ? "E" : "N", false);
}

function clearAll(){
  if(!confirm("Clear message history on this node?")) return;
  fetch("/clear", {method: "POST"})
    .then(function(){ state.ver = -1; });
}

function updateCount(){
  $("cnt").textContent = $("msg").value.length;
}

function buildChips(){
  var html = "";
  for(var i = 0; i < PRESETS.length; i++){
    html += '<button class="chip" onclick="usePreset(' + i + ')">' + esc(PRESETS[i]) + '</button>';
  }
  $("chips").innerHTML = html;
}

function usePreset(i){
  $("msg").value = PRESETS[i];
  updateCount();
}

buildChips();
updateCount();
poll();
</script>

</body>
</html>
)rawliteral";

// ============================================================
// INTERRUPT
// ============================================================

void IRAM_ATTR setFlag(void) {
  packetReceived = true;
}

// ============================================================
// HELPERS
// ============================================================

String uint64ToString(uint64_t value) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
  return String(buffer);
}

uint64_t stringToUint64(const String& value) {
  return strtoull(value.c_str(), nullptr, 10);
}

String getField(const String& packet, int fieldNumber) {
  int start = 0;

  for (int i = 0; i < fieldNumber; i++) {
    int separator = packet.indexOf('|', start);
    if (separator < 0) {
      return "";
    }
    start = separator + 1;
  }

  int end = packet.indexOf('|', start);
  if (end < 0) {
    end = packet.length();
  }

  return packet.substring(start, end);
}

String jsonEscape(const String& input) {
  String output = "";

  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);

    if (c == '"') {
      output += "\\\"";
    } else if (c == '\\') {
      output += "\\\\";
    } else if (c == '\n') {
      output += "\\n";
    } else if (c == '\r') {
      output += "\\r";
    } else if (c == '\t') {
      output += "\\t";
    } else {
      output += c;
    }
  }

  return output;
}

// ============================================================
// HISTORY
// ============================================================

void addHistory(
  const String& direction,
  const String& type,
  const String& text,
  uint64_t id,
  int rssi,
  float snr,
  uint8_t status
) {
  if (historyCount >= HISTORY_SIZE) {
    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
      history[i] = history[i + 1];
    }
    historyCount = HISTORY_SIZE - 1;
  }

  history[historyCount].direction = direction;
  history[historyCount].type = type;
  history[historyCount].text = text;
  history[historyCount].id = id;
  history[historyCount].rssi = rssi;
  history[historyCount].snr = snr;
  history[historyCount].status = status;
  history[historyCount].ms = millis();

  historyCount++;
  historyVersion++;
}

void setTxStatus(uint64_t id, uint8_t status, int rssi, float snr) {
  for (int i = historyCount - 1; i >= 0; i--) {
    if (history[i].direction == "TX" && history[i].id == id) {
      history[i].status = status;
      history[i].rssi = rssi;
      history[i].snr = snr;
      historyVersion++;
      return;
    }
  }
}

// ============================================================
// GCM NONCE / AAD
// ============================================================

void buildNonce(
  uint8_t sender,
  uint8_t destination,
  uint8_t domain,
  uint64_t id,
  uint8_t nonce[12]
) {
  nonce[0] = sender;
  nonce[1] = domain;
  nonce[2] = destination;
  nonce[3] = 0xA5;

  for (int i = 0; i < 8; i++) {
    nonce[4 + i] = (uint8_t)((id >> (56 - (i * 8))) & 0xFF);
  }
}

String buildMessageAAD(uint8_t source, uint8_t destination, uint64_t id, char type) {
  String aad = "";
  aad += PROTOCOL_VERSION;
  aad += "|MSG|";
  aad += String(source);
  aad += "|";
  aad += String(destination);
  aad += "|";
  aad += uint64ToString(id);
  aad += "|";
  aad += type;
  return aad;
}

String buildAckAAD(uint8_t source, uint8_t destination, uint64_t id) {
  String aad = "";
  aad += PROTOCOL_VERSION;
  aad += "|ACK|";
  aad += String(source);
  aad += "|";
  aad += String(destination);
  aad += "|";
  aad += uint64ToString(id);
  return aad;
}

// ============================================================
// BASE64
// ============================================================

String base64Encode(const uint8_t* data, size_t length) {
  size_t required = 0;

  mbedtls_base64_encode(nullptr, 0, &required, data, length);

  unsigned char* output = new unsigned char[required + 1];
  size_t actual = 0;

  int result = mbedtls_base64_encode(output, required + 1, &actual, data, length);

  if (result != 0) {
    delete[] output;
    return "";
  }

  output[actual] = '\0';
  String encoded = String((char*)output);
  delete[] output;

  return encoded;
}

bool base64Decode(
  const String& input,
  uint8_t* output,
  size_t outputSize,
  size_t& actual
) {
  actual = 0;

  int result = mbedtls_base64_decode(
    output,
    outputSize,
    &actual,
    (const unsigned char*)input.c_str(),
    input.length()
  );

  return result == 0;
}

// ============================================================
// AES-GCM
// ============================================================

bool encryptGCM(
  const String& plaintext,
  uint8_t sender,
  uint8_t destination,
  uint8_t domain,
  uint64_t id,
  const String& aad,
  String& ciphertextBase64,
  String& tagBase64
) {
  uint8_t nonce[12];
  uint8_t tag[16];

  buildNonce(sender, destination, domain, id, nonce);

  size_t length = plaintext.length();
  uint8_t* ciphertext = new uint8_t[length + 1];

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);

  int result = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128);

  if (result != 0) {
    mbedtls_gcm_free(&ctx);
    delete[] ciphertext;
    return false;
  }

  result = mbedtls_gcm_crypt_and_tag(
    &ctx,
    MBEDTLS_GCM_ENCRYPT,
    length,
    nonce,
    sizeof(nonce),
    (const unsigned char*)aad.c_str(),
    aad.length(),
    (const unsigned char*)plaintext.c_str(),
    ciphertext,
    sizeof(tag),
    tag
  );

  mbedtls_gcm_free(&ctx);

  if (result != 0) {
    delete[] ciphertext;
    return false;
  }

  ciphertextBase64 = base64Encode(ciphertext, length);
  tagBase64 = base64Encode(tag, sizeof(tag));

  delete[] ciphertext;

  return (ciphertextBase64.length() > 0 && tagBase64.length() > 0);
}

bool decryptGCM(
  const String& ciphertextBase64,
  const String& tagBase64,
  uint8_t sender,
  uint8_t destination,
  uint8_t domain,
  uint64_t id,
  const String& aad,
  String& plaintext
) {
  uint8_t ciphertext[MAX_TEXT_LENGTH + 16];
  uint8_t tag[16];

  size_t ciphertextLength = 0;
  size_t tagLength = 0;

  if (!base64Decode(ciphertextBase64, ciphertext, sizeof(ciphertext), ciphertextLength)) {
    return false;
  }

  if (!base64Decode(tagBase64, tag, sizeof(tag), tagLength)) {
    return false;
  }

  if (tagLength != 16) {
    return false;
  }

  if (ciphertextLength > MAX_TEXT_LENGTH) {
    return false;
  }

  uint8_t nonce[12];
  buildNonce(sender, destination, domain, id, nonce);

  uint8_t output[MAX_TEXT_LENGTH + 1];

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);

  int result = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128);

  if (result != 0) {
    mbedtls_gcm_free(&ctx);
    return false;
  }

  result = mbedtls_gcm_auth_decrypt(
    &ctx,
    ciphertextLength,
    nonce,
    sizeof(nonce),
    (const unsigned char*)aad.c_str(),
    aad.length(),
    tag,
    16,
    ciphertext,
    output
  );

  mbedtls_gcm_free(&ctx);

  if (result != 0) {
    return false;
  }

  output[ciphertextLength] = '\0';
  plaintext = String((char*)output);

  return true;
}

// ============================================================
// OLED
// ============================================================

#define OLED_IDLE_MS 25000

unsigned long oledLastEvent = 0;
bool oledBrand = false;


// Brand screen: shield + keyhole logo and the DURBHAASH wordmark
void showBrand(const char* bottom) {
  display.clearBuffer();

  // Shield logo (filled) with a keyhole cut out
  display.setDrawColor(1);
  display.drawBox(5, 6, 24, 14);
  display.drawTriangle(5, 19, 28, 19, 16, 40);
  display.setDrawColor(0);
  display.drawDisc(16, 14, 3);
  display.drawBox(15, 16, 3, 9);
  display.setDrawColor(1);

  // Wordmark
  display.setFont(u8g2_font_7x13B_tr);
  display.drawStr(38, 22, "DURBHAASH");

  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(38, 36, ("NODE " + String(NODE_ID)).c_str());

  
  display.drawHLine(0, 46, 128);
  display.drawStr(0, 60, bottom);

  display.sendBuffer();

  oledBrand = true;
  oledLastEvent = millis();
}

void updateOLED(
  const String& line1,
  const String& line2,
  const String& line3,
  const String& line4
) {
  oledBrand = false;
  oledLastEvent = millis();

  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);

  display.drawStr(0, 10, line1.c_str());
  display.drawStr(0, 25, line2.c_str());
  display.drawStr(0, 40, line3.c_str());
  display.drawStr(0, 55, line4.c_str());

  display.sendBuffer();
}

// ============================================================
// REPLAY PROTECTION (sliding window of 32 IDs)
// Returns true if the ID must be rejected (already seen / too old).
// Marks the ID as seen otherwise.
// ============================================================

bool isReplay(uint64_t id) {
  if (id > rxHighest) {

    uint64_t shift = id - rxHighest;

    if (shift >= 32) {
      rxWindow = 0;
    } else {
      rxWindow <<= shift;
    }

    rxWindow |= 1;
    rxHighest = id;

    preferences.putULong64("rxhigh", rxHighest);

    return false;
  }

  uint64_t diff = rxHighest - id;

  if (diff >= 32) {
    return true;
  }

  if (rxWindow & (1UL << diff)) {
    return true;
  }

  rxWindow |= (1UL << diff);

  return false;
}

// ============================================================
// SEND ACK
// ============================================================

bool sendACK(uint8_t destination, uint64_t originalID) {
  String encrypted;
  String tag;

  String aad = buildAckAAD(NODE_ID, destination, originalID);

  if (!encryptGCM("OK", NODE_ID, destination, DOMAIN_ACK, originalID, aad, encrypted, tag)) {
    Serial.println("ACK encryption failed");
    return false;
  }

  String packet =
    String(PROTOCOL_VERSION) + "|ACK|" +
    String(NODE_ID) + "|" +
    String(destination) + "|" +
    uint64ToString(originalID) + "|" +
    encrypted + "|" +
    tag;

  int result = radio.transmit(packet);

  radio.startReceive();
  packetReceived = false;

  return result == RADIOLIB_ERR_NONE;
}

// ============================================================
// PROCESS ACK
// ============================================================

void processACK(const String& packet) {
  uint8_t source = getField(packet, 2).toInt();
  uint8_t destination = getField(packet, 3).toInt();
  uint64_t id = stringToUint64(getField(packet, 4));
  String ciphertext = getField(packet, 5);
  String tag = getField(packet, 6);

  if (source != DEST_ID ||
      destination != NODE_ID ||
      !txActive ||
      id != txID) {
    return;
  }

  String plaintext;
  String aad = buildAckAAD(source, destination, id);

  if (!decryptGCM(ciphertext, tag, source, destination, DOMAIN_ACK, id, aad, plaintext)) {
    Serial.println("Invalid ACK authentication");
    return;
  }

  if (plaintext == "OK") {
    ackReceived = true;
    ackRSSI = curRSSI;
    ackSNR = curSNR;

    lastRSSI = curRSSI;
    lastSNR = curSNR;
    lastRxMillis = millis();

    Serial.print("ACK received for ID ");
    Serial.println(uint64ToString(id));
  }
}

// ============================================================
// PROCESS MESSAGE
// ============================================================

void processMessage(const String& packet) {
  uint8_t source = getField(packet, 2).toInt();
  uint8_t destination = getField(packet, 3).toInt();
  uint64_t id = stringToUint64(getField(packet, 4));
  String typeField = getField(packet, 5);
  String ciphertext = getField(packet, 6);
  String tag = getField(packet, 7);

  if (source != DEST_ID || destination != NODE_ID) {
    return;
  }

  char type = typeField.length() ? typeField.charAt(0) : 'N';

  if (type != 'N' && type != 'E') {
    return;
  }

  String plaintext;
  String aad = buildMessageAAD(source, destination, id, type);

  if (!decryptGCM(ciphertext, tag, source, destination, DOMAIN_MESSAGE, id, aad, plaintext)) {
    Serial.println("MESSAGE AUTHENTICATION FAILED");
    updateOLED("DURBHAASH", "AUTH FAILED", "INVALID MESSAGE", "");
    return;
  }

  // Packet is authentic: record link quality
  lastRSSI = curRSSI;
  lastSNR = curSNR;
  lastRxMillis = millis();

  // Replay / duplicate protection
  if (isReplay(id)) {
    Serial.println("Replay / duplicate detected - re-sending ACK");
    sendACK(source, id);
    return;
  }

  lastMessage = plaintext;
  lastMessageType = (type == 'E') ? "Emergency" : "Normal";
  lastMessageSource = String(source);

  statReceived++;

  addHistory("RX", lastMessageType, plaintext, id, lastRSSI, lastSNR, ST_DELIVERED);

  Serial.println();
  Serial.println("===== MESSAGE RECEIVED =====");
  Serial.print("From: ");
  Serial.println(source);
  Serial.print("ID: ");
  Serial.println(uint64ToString(id));
  Serial.print("Type: ");
  Serial.println(lastMessageType);
  Serial.print("Message: ");
  Serial.println(plaintext);
  Serial.print("RSSI: ");
  Serial.println(lastRSSI);
  Serial.print("SNR: ");
  Serial.println(lastSNR);
  Serial.println("============================");

  String shortText = plaintext.substring(0, min(18, (int)plaintext.length()));

  updateOLED(
    type == 'E' ? "!!! EMERGENCY !!!" : "MESSAGE RECEIVED",
    "FROM NODE 2",
    shortText,
    "RSSI " + String(lastRSSI)
  );

  sendACK(source, id);
}

// ============================================================
// PROCESS INCOMING
// ============================================================

void processIncomingPacket() {
  String packet;

  int result = radio.readData(packet);

  // Capture signal info immediately after reading
  curRSSI = (int)radio.getRSSI();
  curSNR = radio.getSNR();

  if (result != RADIOLIB_ERR_NONE) {
    Serial.print("RX error: ");
    Serial.println(result);
    radio.startReceive();
    packetReceived = false;
    return;
  }

  Serial.print("Received: ");
  Serial.println(packet);

  if (packet.startsWith("V3|ACK|")) {
    processACK(packet);
  } else if (packet.startsWith("V3|MSG|")) {
    processMessage(packet);
  } else {
    Serial.println("Unknown packet");
  }

  radio.startReceive();
  packetReceived = false;
}

// ============================================================
// NON-BLOCKING SEND (state machine)
// ============================================================

void doTransmit() {
  txAttempt++;
  ackReceived = false;

  Serial.print("Attempt ");
  Serial.print(txAttempt);
  Serial.print("/");
  Serial.println(txMaxAttempts);

  int result = radio.transmit(txPacket);

  if (result != RADIOLIB_ERR_NONE) {
    Serial.print("TX error: ");
    Serial.println(result);
  }

  radio.startReceive();
  packetReceived = false;

  txLastTime = millis();

  // Timeout = airtime of an ACK + processing margin + random backoff
  unsigned long ackAirMs = (unsigned long)(radio.getTimeOnAir(64) / 1000);
  txTimeout = ackAirMs + 700 + random(0, 400);

  updateOLED(
    "SENDING...",
    "TO NODE 2",
    "TRY " + String(txAttempt) + "/" + String(txMaxAttempts),
    "ID " + uint64ToString(txID)
  );
}

// Returns the new message ID, or 0 if it could not be started.
uint64_t startSend(const String& text, char type) {
  if (txActive) {
    return 0;
  }

  if (text.length() == 0 || text.length() > MAX_TEXT_LENGTH) {
    return 0;
  }

  txCounter++;
  preferences.putULong64("txcounter", txCounter);

  uint64_t messageID = txCounter;

  String ciphertext;
  String tag;

  String aad = buildMessageAAD(NODE_ID, DEST_ID, messageID, type);

  if (!encryptGCM(text, NODE_ID, DEST_ID, DOMAIN_MESSAGE, messageID, aad, ciphertext, tag)) {
    Serial.println("Message encryption failed");
    return 0;
  }

  txPacket =
    String(PROTOCOL_VERSION) + "|MSG|" +
    String(NODE_ID) + "|" +
    String(DEST_ID) + "|" +
    uint64ToString(messageID) + "|" +
    String(type) + "|" +
    ciphertext + "|" +
    tag;

  txText = text;
  txType = type;
  txID = messageID;
  txAttempt = 0;
  txMaxAttempts = (type == 'E') ? RETRIES_EMERGENCY : RETRIES_NORMAL;
  txActive = true;

  statSent++;

  addHistory(
    "TX",
    type == 'E' ? "Emergency" : "Normal",
    text,
    messageID,
    0,
    0,
    ST_SENDING
  );

  Serial.println();
  Serial.println("===== TRANSMITTING =====");
  Serial.print("ID: ");
  Serial.println(uint64ToString(messageID));
  Serial.print("Packet length: ");
  Serial.println(txPacket.length());

  doTransmit();

  return messageID;
}

void finishTx(bool ok) {
  txActive = false;

  if (ok) {
    statDelivered++;
    setTxStatus(txID, ST_DELIVERED, ackRSSI, ackSNR);

    updateOLED(
      "MESSAGE SENT",
      "TO NODE 2",
      "ACK RECEIVED",
      "ID " + uint64ToString(txID)
    );

    Serial.println("DELIVERY CONFIRMED");

  } else {
    statFailed++;
    setTxStatus(txID, ST_FAILED, 0, 0);

    updateOLED("MESSAGE SENT", "TO NODE 2", "NO ACK", "DELIVERY UNKNOWN");

    Serial.println("NO ACK RECEIVED");
  }
}

void txTick() {
  if (!txActive) {
    return;
  }

  if (ackReceived) {
    finishTx(true);
    return;
  }

  if (millis() - txLastTime >= txTimeout) {
    if (txAttempt >= txMaxAttempts) {
      finishTx(false);
    } else {
      doTransmit();
    }
  }
}

// ============================================================
// HTTP HANDLERS
// ============================================================

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleSend() {
  if (!server.hasArg("message")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing message\"}");
    return;
  }

  if (txActive) {
    server.send(409, "application/json", "{\"success\":false,\"message\":\"Radio is still sending the previous message\"}");
    return;
  }

  String message = server.arg("message");
  message.trim();

  if (message.length() == 0 || message.length() > MAX_TEXT_LENGTH) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid message length\"}");
    return;
  }

  char type = 'N';

  if (server.hasArg("type")) {
    String typeString = server.arg("type");
    if (typeString.length() > 0 && typeString.charAt(0) == 'E') {
      type = 'E';
    }
  }

  uint64_t id = startSend(message, type);

  if (id == 0) {
    server.send(500, "application/json", "{\"success\":false,\"message\":\"Could not start transmission\"}");
    return;
  }

  String json = "{\"success\":true,\"message\":\"Queued\",\"id\":" + uint64ToString(id) + "}";

  server.send(200, "application/json", json);
}

int signalQuality() {
  if (lastRxMillis == 0) {
    return 0;
  }

  int q;

  if (lastRSSI > -80) {
    q = 4;
  } else if (lastRSSI > -95) {
    q = 3;
  } else if (lastRSSI > -105) {
    q = 2;
  } else {
    q = 1;
  }

  if (lastSNR < -5.0 && q > 1) {
    q = 1;
  }

  return q;
}

void handleStatus() {
  long age = (lastRxMillis == 0) ? -1 : (long)((millis() - lastRxMillis) / 1000);

  String json = "{";

  json += "\"node\":" + String(NODE_ID);
  json += ",\"destination\":" + String(DEST_ID);
  json += ",\"frequency\":" + String(LORA_FREQUENCY, 1);
  json += ",\"sf\":" + String(LORA_SF);
  json += ",\"rssi\":" + String(lastRSSI);
  json += ",\"snr\":" + String(lastSNR, 1);
  json += ",\"quality\":" + String(signalQuality());
  json += ",\"lastRxAge\":" + String(age);
  json += ",\"ver\":" + String(historyVersion);
  json += ",\"txBusy\":" + String(txActive ? "true" : "false");
  json += ",\"txAttempt\":" + String(txAttempt);
  json += ",\"txMax\":" + String(txMaxAttempts);
  json += ",\"sent\":" + String(statSent);
  json += ",\"delivered\":" + String(statDelivered);
  json += ",\"failed\":" + String(statFailed);
  json += ",\"received\":" + String(statReceived);
  json += ",\"uptime\":" + String(millis() / 1000);

  json += "}";

  server.send(200, "application/json", json);
}

void handleHistory() {
  String json = "[";

  for (int i = 0; i < historyCount; i++) {
    if (i > 0) {
      json += ",";
    }

    json += "{";
    json += "\"dir\":\"" + jsonEscape(history[i].direction) + "\"";
    json += ",\"type\":\"" + jsonEscape(history[i].type) + "\"";
    json += ",\"text\":\"" + jsonEscape(history[i].text) + "\"";
    json += ",\"id\":" + uint64ToString(history[i].id);
    json += ",\"rssi\":" + String(history[i].rssi);
    json += ",\"snr\":" + String(history[i].snr, 1);
    json += ",\"status\":" + String(history[i].status);
    json += ",\"age\":" + String((millis() - history[i].ms) / 1000);
    json += "}";
  }

  json += "]";

  server.send(200, "application/json", json);
}

void handleClearHistory() {
  historyCount = 0;
  historyVersion++;

  lastMessage = "";
  lastMessageType = "";
  lastMessageSource = "";

  statSent = 0;
  statDelivered = 0;
  statFailed = 0;
  statReceived = 0;

  server.send(200, "application/json", "{\"success\":true}");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

// ============================================================
// WIFI / WEB SERVER
// ============================================================

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME, AP_PASSWORD);

  Serial.println();
  Serial.println("===== WIFI =====");
  Serial.print("SSID: ");
  Serial.println(AP_NAME);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/clear", HTTP_POST, handleClearHistory);
  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println("Web server started");
}

// ============================================================
// RADIO
// ============================================================

bool setupRadio() {
  Serial.println("Initializing SX1262...");

  int state = radio.begin(
    LORA_FREQUENCY,
    LORA_BANDWIDTH,
    LORA_SF,
    LORA_CR,
    LORA_SYNCWORD,
    LORA_POWER,
    LORA_PREAMBLE
  );

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Radio initialization failed: ");
    Serial.println(state);
    return false;
  }

  radio.setDio2AsRfSwitch(true);
  radio.setRxBoostedGainMode(true);
  radio.setPacketReceivedAction(setFlag);

  state = radio.startReceive();

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Receive start failed: ");
    Serial.println(state);
    return false;
  }

  Serial.println("SX1262 initialized");

  return true;
}

// ============================================================
// OLED SETUP
// ============================================================

void setupOLED() {
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);

  delay(100);

  display.begin();

  showBrand("INITIALIZING...");

  delay(1000);
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("DURBHAASH NODE 1");
  Serial.println("ESP32-S3 + SX1262");
  Serial.println("================================");

  setupOLED();

  preferences.begin("securelora", false);

  txCounter = preferences.getULong64("txcounter", 0);
  rxHighest = preferences.getULong64("rxhigh", 0);
  rxWindow = 0xFFFFFFFF;   // after reboot, treat everything <= rxHighest as seen

  Serial.print("TX Counter: ");
  Serial.println(uint64ToString(txCounter));
  Serial.print("RX Highest: ");
  Serial.println(uint64ToString(rxHighest));

  if (!setupRadio()) {
    updateOLED("RADIO ERROR", "SX1262 FAILED", "CHECK RADIO", "");

    while (true) {
      delay(1000);
    }
  }

  setupWiFi();
  setupWebServer();

  showBrand("SECURE LORA - READY");

  Serial.println("NODE 1 READY");
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  server.handleClient();

  if (packetReceived) {
    packetReceived = false;
    processIncomingPacket();
  }

  txTick();

  // 25 s after the last message / ACK / failure, return to the brand screen
  if (!txActive && !oledBrand && millis() - oledLastEvent > OLED_IDLE_MS) {
    showBrand("SECURE LORA - READY");
  }

  delay(2);
}
