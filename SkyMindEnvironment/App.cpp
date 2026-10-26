#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <OneWire.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <errno.h>
#include "Config.h"
#include "Protocol.h"
#include "RS485Transport.h"
#include "Commission.h"
#include "Dashboard.h"
#include "CloudTelemetry.h"
#include "CloudConfig.h"

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error This pin map requires a classic ESP32-WROOM, not C3/S3/S2.
#endif

namespace {
struct BusConfig { uint32_t baud; uint8_t framing; bool automatic; };
struct SensorConfig {
  bool enabled; uint8_t id, fn, format; uint16_t reg;
  bool swapBytes; double scale, offset; uint32_t normalRaw, alarmRaw;
};
struct Settings { uint32_t version; BusConfig buses[2]; SensorConfig sensors[5]; } settings;
struct Reading {
  bool valid=false, ever=false; double value=0; uint32_t lastOk=0, lastTry=0, good=0, bad=0; uint64_t cloudLastOk=0;
  String error="Needs setup", raw="";
} readings[6];
const char* names[]={"Wind","Precipitation","Light","Humidity","Water ingress","Temperature"};
const char* units[]={"m/s","","lx","% RH","","°C"};
const uint8_t sensorBus[]={0,0,0,1,1};
WebServer server(80);
Preferences prefs;
HardwareSerial outdoor(2), indoor(1);
HardwareSerial* ports[]={&outdoor,&indoor};
const int rxPins[]={OUTDOOR_RX,INDOOR_RX},txPins[]={OUTDOOR_TX,INDOOR_TX},dirPins[]={OUTDOOR_DIR,INDOOR_DIR};
const uint32_t serialModes[]={SERIAL_8N1,SERIAL_8E1,SERIAL_8O1,SERIAL_8N2};
const char* serialNames[]={"8N1","8E1","8O1","8N2"};
OneWire wire(TEMPERATURE_PIN);
uint8_t tempRom[8];bool tempFound=false,tempConverting=false;
uint32_t tempStarted=0,tempScan=0,tempAttempt=0,lastBusEnd[2]={0,0};
uint32_t wifiLostSince=0;
bool apStarted=false,mdnsStarted=false,wasConnected=false,prefsReady=false;
String csrf,apName,apPassword;
enum class WifiPhase { Disconnected, Connecting, Connected };
WifiPhase wifiPhase=WifiPhase::Disconnected;
uint32_t wifiPhaseSince=0,ledChanged=0;bool ledLit=false;

struct Transaction {
  bool active=false; uint8_t bus=0,id=0,fn=0; uint16_t address=0,count=0;
  int sensor=-1; uint8_t tx[8],rx[128];size_t size=0;
  uint32_t start=0,lastByte=0;
} tr;
struct ProbeResult { String state="Idle",tx="",rx="",error="",values="";uint32_t number=0; } probe;
uint8_t nextSensor=0;
bool setupPaused=true,commissionSaved=false,commissionRestored=true;
uint32_t runtimeBaud[2]={9600,9600};
uint32_t configRevision=0;
Commission::Job commission;
String commissionTx,commissionRx,commissionTransport,commissionResult;
SensorConfig documentedProfile(uint8_t i) {
  auto p=Commission::profile(i);SensorConfig c={};
  c.id=p.id;c.fn=3;c.format=p.format;c.reg=p.reg;c.scale=p.scale;c.alarmRaw=1;
  return c;
}

bool elapsed(uint32_t now,uint32_t then,uint32_t ms) {return uint32_t(now-then)>=ms;}
bool isAlarmSensor(uint8_t i) {return i==1||i==4;}
String quote(const String &s) {
  String o="\"";o.reserve(s.length()+8);
  for(size_t i=0;i<s.length();i++) {char c=s[i];if(c=='"'||c=='\\'){o+='\\';o+=c;}
    else if(uint8_t(c)<32)o+=' ';else o+=c;}
  return o+'"';
}
String hex(const uint8_t *p,size_t n) {
  String s;s.reserve(n*3);char b[4];for(size_t i=0;i<n;i++){snprintf(b,sizeof b,"%02X",p[i]);if(i)s+=' ';s+=b;}return s;
}
String rawRegisters(const uint16_t *r,uint16_t count) {
  String s;char b[20];for(uint16_t i=0;i<count;i++){if(i)s+=", ";snprintf(b,sizeof b,"0x%04X (%u)",r[i],r[i]);s+=b;}return s;
}
void fail(uint8_t i,const String &error) { readings[i].valid=false;readings[i].error=error;readings[i].bad++; }
void success(uint8_t i,double value) {auto &r=readings[i];r.valid=true;r.ever=true;r.value=value;r.lastOk=millis();r.cloudLastOk=telemetryUptimeMs();r.error="";r.good++;}
bool validBus(const BusConfig &b) {
  return (b.baud==1200||b.baud==2400||b.baud==4800||b.baud==9600||b.baud==19200||b.baud==38400||b.baud==57600||b.baud==115200)&&b.framing<4;
}
bool validSensor(uint8_t i,const SensorConfig &c) {
  if(c.format>7||!isfinite(c.scale)||c.scale==0||!isfinite(c.offset))return false;
  if(!Modbus::validRequest(c.id,c.fn,c.reg,c.fn<=2?1:Modbus::registerCount(c.format)))return false;
  if(c.fn<=2 && (!isAlarmSensor(i)||c.format!=0||c.swapBytes))return false;
  if(isAlarmSensor(i)) {
    if(c.format!=0 && c.format!=2 && c.format!=3)return false;
    if(c.normalRaw==c.alarmRaw)return false;
    if(c.fn<=2 && (c.normalRaw>1||c.alarmRaw>1))return false;
    if(c.format==0 && (c.normalRaw>65535||c.alarmRaw>65535))return false;
  }
  return true;
}
void defaults() {
  memset(&settings,0,sizeof settings);settings.version=1;
  for(auto &b:settings.buses){b.baud=9600;b.framing=0;b.automatic=false;}
  settings.buses[0].baud=4800;
  for(uint8_t i=0;i<5;i++)settings.sensors[i]=documentedProfile(i);
}
bool validSettings(const Settings &s) {
  if(s.version!=1)return false;
  for(auto &b:s.buses)if(!validBus(b))return false;
  for(uint8_t i=0;i<5;i++){
    if(!validSensor(i,s.sensors[i]))return false;
    for(uint8_t j=0;j<i;j++)if(s.sensors[i].enabled&&s.sensors[j].enabled&&sensorBus[i]==sensorBus[j]&&s.sensors[i].id==s.sensors[j].id)return false;
  }return true;
}
bool saveSettings(const Settings &candidate) {
  if(!prefsReady||prefs.putBytes("settings",&candidate,sizeof candidate)!=sizeof candidate)return false;
  settings=candidate;configRevision++;return true;
}
void beginBus(uint8_t b,uint32_t overrideBaud=0) {
  runtimeBaud[b]=overrideBaud?overrideBaud:settings.buses[b].baud;
  ports[b]->end();
  // Manual DE+/RE is low at idle. In auto mode leave the direction GPIO unused.
  if(settings.buses[b].automatic)pinMode(dirPins[b],INPUT);
  else{digitalWrite(dirPins[b],LOW);pinMode(dirPins[b],OUTPUT);}
  ports[b]->setRxBufferSize(256);
  ports[b]->begin(runtimeBaud[b],serialModes[settings.buses[b].framing],rxPins[b],txPins[b]);
  // SP3485 RO is high impedance while RSE is high. Hold UART RX at idle
  // without pinMode(), which can detach its UART routing in Arduino-ESP32 3.x.
  gpio_set_pull_mode(static_cast<gpio_num_t>(rxPins[b]),GPIO_PULLUP_ONLY);
  lastBusEnd[b]=millis();
}
uint32_t gapMs(uint8_t bus) {
  uint32_t baud=runtimeBaud[bus];
  // At least 3.5 character times at low baud; >= 1.75 ms above 19200.
  return baud>19200?2:(38500+baud-1)/baud+1;
}
bool startTransaction(uint8_t bus,uint8_t id,uint8_t fn,uint16_t addr,uint16_t count,int sensor) {
  if(bus>1||tr.active||!elapsed(millis(),lastBusEnd[bus],gapMs(bus)))return false;
  if(fn==6){
    if(sensor!=-2||!commission.active()||(commission.stage!=Commission::Stage::Write&&commission.stage!=Commission::Stage::WriteBaud)||
       !Commission::allowedWrite(commission.sensor,id,addr,count,commission.targetBaud))return false;
  }else if(!Modbus::validRequest(id,fn,addr,count))return false;
  tr.bus=bus;tr.id=id;tr.fn=fn;tr.address=addr;tr.count=count;tr.sensor=sensor;tr.size=0;
  auto &p=*ports[bus];while(p.available())p.read();
  Modbus::request(tr.tx,id,fn,addr,count);
  RS485::transmit(p,tr.tx,sizeof tr.tx,settings.buses[bus].automatic,[bus](bool transmitting){
    digitalWrite(dirPins[bus],transmitting?HIGH:LOW);
    if(transmitting)delayMicroseconds(30);
  });
  tr.start=millis();tr.lastByte=tr.start;tr.active=true;
  if(sensor>=0)readings[sensor].lastTry=tr.start;
  else if(sensor==-1){probe.state="Reading";probe.tx=hex(tr.tx,8);probe.rx="";probe.error="";probe.values="";probe.number++;}
  return true;
}
void finishTransaction(const String &transportError="") {
  uint16_t regs[8]={0};uint8_t exception=0;String error=transportError;
  if(error.isEmpty()) {
    auto e=tr.fn==6?Modbus::parseWrite(tr.rx,tr.size,tr.tx,exception):Modbus::parse(tr.rx,tr.size,tr.id,tr.fn,tr.count,regs,exception);
    if(e!=Modbus::OK){error=Modbus::message(e);if(e==Modbus::EXCEPTION)error+=" "+String(exception);}
  }
  if(tr.sensor==-2){
    commissionRx=hex(tr.rx,tr.size);commissionTransport=error;
    commission.reply(error.isEmpty(),regs,millis());
  }else if(tr.sensor==-1){
    probe.rx=hex(tr.rx,tr.size);probe.error=error;probe.state=error.isEmpty()?"Reply received":"Failed";
    if(error.isEmpty())probe.values=rawRegisters(regs,tr.count);
  } else {
    uint8_t i=tr.sensor;auto &r=readings[i];r.raw=hex(tr.rx,tr.size);
    if(!error.isEmpty())fail(i,error);
    else {
      const auto &c=settings.sensors[i];uint32_t bits=0;
      double v=Modbus::decode(regs,c.format,c.swapBytes,bits);
      if(isAlarmSensor(i)){
        if(bits==c.normalRaw)success(i,0);
        else if(bits==c.alarmRaw)success(i,1);
        else fail(i,"Unmapped status code: "+String(bits));
      }else{
        v=v*c.scale+c.offset;
        if(!isfinite(v)||v<0||(i==3&&v>100))fail(i,"Invalid value / check register, scale and units");
        else success(i,v);
      }
    }
  }
  tr.active=false;lastBusEnd[tr.bus]=millis();
}
void serviceTransaction() {
  if(!tr.active)return;
  auto &p=*ports[tr.bus];
  while(p.available()) {
    int c=p.read();if(c<0)break;
    if(tr.size>=sizeof tr.rx){finishTransaction("Reply too long");return;}
    tr.rx[tr.size++]=uint8_t(c);tr.lastByte=millis();
  }
  uint32_t now=millis();
  if(tr.size && elapsed(now,tr.lastByte,gapMs(tr.bus)))finishTransaction();
  else if(!tr.size && elapsed(now,tr.start,REPLY_TIMEOUT_MS))finishTransaction("No reply");
  else if(elapsed(now,tr.start,REPLY_TIMEOUT_MS+50))finishTransaction("Incomplete reply / timeout");
}
void serviceCommission() {
  if(tr.active)return;
  if(commission.stage==Commission::Stage::Done&&!commissionSaved){
    Settings candidate=settings;
    candidate.sensors[commission.sensor]=documentedProfile(commission.sensor);
    candidate.sensors[commission.sensor].id=commission.finalId();
    candidate.sensors[commission.sensor].enabled=true;
    bool outdoorChanged=commission.sensor==1&&candidate.buses[0].baud!=commission.targetBaud;
    if(commission.sensor==1){
      candidate.buses[0].baud=commission.targetBaud;
      if(outdoorChanged){candidate.sensors[0].enabled=false;candidate.sensors[2].enabled=false;}
    }
    if(!validSettings(candidate)||!saveSettings(candidate)){
      commission.fail("Sensor replied, but its profile could not be saved. Keep it isolated and retry setup.");
    }else{
      commissionSaved=true;success(commission.sensor,commission.value);
      commissionResult=String(names[commission.sensor])+" communication verified at ID "+String(commission.finalId())+
        ", "+String(commission.targetBaud)+" baud. Sample: "+String(commission.value,1)+" "+units[commission.sensor]+". Profile saved.";
      if(commission.sensor==1)commissionResult+=" Check that wetting/drying the sensing surface changes Rain/Dry before relying on the reading.";
      if(outdoorChanged)commissionResult+=" Outdoor speed changed: run Wind and Light setup next, even if you set them up in v2.";
      commissionResult+=" Polling stays paused until you reconnect the configured sensors and resume.";
    }
  }
  if(!commission.active()&&!commissionRestored){
    beginBus(Commission::profile(commission.sensor).bus);commissionRestored=true;
  }
  Commission::Request request;
  if(commission.next(millis(),request)){
    uint8_t bus=Commission::profile(commission.sensor).bus;
    if(runtimeBaud[bus]!=request.baud){beginBus(bus,request.baud);return;}
    if(startTransaction(bus,request.id,request.fn,request.reg,request.value,-2)){
      commission.sent();commissionTx=hex(tr.tx,8);commissionRx="";commissionTransport="";
    }
  }
}
void pollSensors() {
  if(tr.active||setupPaused||commission.active())return;
  for(uint8_t n=0;n<5;n++) {
    uint8_t i=nextSensor;nextSensor=(nextSensor+1)%5;
    const auto &c=settings.sensors[i];
    if(c.enabled&&elapsed(millis(),readings[i].lastTry,POLL_MS)) {
      if(startTransaction(sensorBus[i],c.id,c.fn,c.reg,c.fn<=2?1:Modbus::registerCount(c.format),i))return;
    }
  }
}
void scanTemperature() {
  uint8_t address[8];uint8_t count=0;wire.reset_search();
  while(wire.search(address))if(address[0]==0x28 && OneWire::crc8(address,7)==address[7]){memcpy(tempRom,address,8);count++;}
  tempFound=count==1;tempScan=millis();
  if(!tempFound)fail(5,count>1?"Connect only one DS18B20":"No DS18B20 found");
  else {readings[5].error="Waiting for conversion";Serial.println("DS18B20 ROM: "+hex(tempRom,8));}
}
void serviceTemperature() {
  uint32_t now=millis();
  if(!tempFound){if(elapsed(now,tempScan,5000))scanTemperature();return;}
  if(tempConverting){
    if(!elapsed(now,tempStarted,800))return;
    tempConverting=false;
    if(!wire.read_bit()){fail(5,"DS18B20 conversion did not finish");return;}
    if(!wire.reset()){tempFound=false;fail(5,"Temperature probe disconnected");return;}
    wire.select(tempRom);wire.write(0xbe);uint8_t scratch[9];wire.read_bytes(scratch,9);
    readings[5].raw=hex(scratch,9);
    bool allZero=true;for(uint8_t x:scratch)if(x)allZero=false;
    if(allZero||OneWire::crc8(scratch,8)!=scratch[8]){fail(5,"DS18B20 CRC / wiring error");return;}
    int16_t raw=int16_t((uint16_t(scratch[1])<<8)|scratch[0]);
    uint8_t resolution=(scratch[4]>>5)&3;
    if(resolution<3)raw&=~((1<<(3-resolution))-1);
    double c=raw/16.0;
    // 85 is also the power-on value. Mark it uncertain instead of displaying a
    // potentially unconverted sample as a verified measurement.
    if(c==85.0)fail(5,"85 C power-on value / verify with another thermometer");
    else if(c< -55||c>125)fail(5,"DS18B20 out of range");else success(5,c);
  } else if(elapsed(now,tempAttempt,POLL_MS)) {
    tempAttempt=now;readings[5].lastTry=now;
    if(!wire.reset()){tempFound=false;fail(5,"Temperature probe disconnected");return;}
    wire.select(tempRom);wire.write(0xb4);
    if(!wire.read_bit()){fail(5,"Probe needs powered 3-wire wiring");return;}
    if(!wire.reset()){tempFound=false;fail(5,"Temperature probe disconnected");return;}
    wire.select(tempRom);wire.write(0x44,0);tempStarted=millis();tempConverting=true;
  }
}
void startFallback() {
  if(apStarted)return;
  WiFi.mode(WIFI_AP_STA);
  if(WiFi.softAP(apName.c_str(),apPassword.c_str())){
    apStarted=true;Serial.println("Fallback Wi-Fi: "+apName);Serial.println("Fallback password: "+apPassword);
    Serial.println("Fallback dashboard: http://"+WiFi.softAPIP().toString());
  }
}
void setLed(bool lit) {ledLit=lit;digitalWrite(STATUS_LED_PIN,(lit==STATUS_LED_ACTIVE_HIGH)?HIGH:LOW);}
void setWifiPhase(WifiPhase phase) {
  wifiPhase=phase;wifiPhaseSince=millis();ledChanged=wifiPhaseSince;
  setLed(phase!=WifiPhase::Disconnected);
}
void serviceLed() {
  if(wifiPhase==WifiPhase::Connected){if(!ledLit)setLed(true);return;}
  uint32_t period=wifiPhase==WifiPhase::Connecting?LED_FAST_HALF_PERIOD_MS:LED_SLOW_HALF_PERIOD_MS;
  if(elapsed(millis(),ledChanged,period)){ledChanged=millis();setLed(!ledLit);}
}
void serviceWifi() {
  bool connected=WiFi.status()==WL_CONNECTED;uint32_t now=millis();
  if(connected&&!wasConnected){
    setWifiPhase(WifiPhase::Connected);
    Serial.println("Dashboard: http://"+WiFi.localIP().toString());
    if(!mdnsStarted){mdnsStarted=MDNS.begin(MDNS_NAME);if(mdnsStarted)MDNS.addService("http","tcp",80);}
  }
  if(!connected&&wasConnected){wifiLostSince=now;WiFi.disconnect(false);setWifiPhase(WifiPhase::Disconnected);}
  if(!connected){
    if(wifiPhase==WifiPhase::Connecting){
      wl_status_t status=WiFi.status();
      bool failed=(status==WL_NO_SSID_AVAIL||status==WL_CONNECT_FAILED)&&elapsed(now,wifiPhaseSince,1000);
      if(failed||elapsed(now,wifiPhaseSince,WIFI_ATTEMPT_MS)){
        WiFi.disconnect(false);setWifiPhase(WifiPhase::Disconnected);
      }
    }else if(elapsed(millis(),wifiPhaseSince,WIFI_RETRY_WAIT_MS)){
      WiFi.begin(WIFI_SSID,WIFI_PASSWORD);setWifiPhase(WifiPhase::Connecting);
    }
    if(elapsed(now,wifiLostSince,25000))startFallback();
  }
  wasConnected=connected;
}
void sendJson(const String &s,int code=200) {server.sendHeader("Cache-Control","no-store");server.send(code,"application/json; charset=utf-8",s);}
void bad(const String &message,int code=400) {sendJson("{\"error\":"+quote(message)+"}",code);}
bool authorized() {if(server.header("X-SkyMind-Token")!=csrf){bad("Reload the dashboard before saving",403);return false;}return true;}
bool integerArg(const char* key,uint32_t min,uint32_t max,uint32_t &value) {
  if(!server.hasArg(key))return false;String s=server.arg(key);s.trim();if(s.isEmpty()||s[0]=='-')return false;
  char *end=nullptr;errno=0;unsigned long v=strtoul(s.c_str(),&end,s.startsWith("0x")||s.startsWith("0X")?16:10);
  if(errno||end==s.c_str()||*end||v<min||v>max)return false;value=v;return true;
}
bool doubleArg(const char* key,double &value) {
  if(!server.hasArg(key))return false;String s=server.arg(key);s.trim();char *end=nullptr;errno=0;
  value=strtod(s.c_str(),&end);return !errno&&end!=s.c_str()&&!*end&&isfinite(value);
}
String configJson() {
  String j="{\"revision\":"+String(configRevision)+",\"token\":"+quote(csrf)+",\"persistent\":"+(prefsReady?"true":"false")+",\"buses\":[";
  for(uint8_t i=0;i<2;i++){if(i)j+=',';auto &b=settings.buses[i];j+="{\"baud\":"+String(b.baud)+",\"framing\":"+String(b.framing)+",\"automatic\":"+(b.automatic?"true":"false")+"}";}
  j+="],\"sensors\":[";
  for(uint8_t i=0;i<5;i++){if(i)j+=',';auto &c=settings.sensors[i];
    j+="{\"enabled\":"+String(c.enabled?"true":"false")+",\"id\":"+String(c.id)+",\"fn\":"+String(c.fn)+",\"reg\":"+String(c.reg)+",\"format\":"+String(c.format)+",\"swapBytes\":"+(c.swapBytes?"true":"false")+",\"scale\":"+String(c.scale,9)+",\"offset\":"+String(c.offset,9)+",\"normalRaw\":"+String(c.normalRaw)+",\"alarmRaw\":"+String(c.alarmRaw)+"}";}
  return j+"]}";
}
// Capture on the original Arduino loop only; the sender never touches sensor/UART state.
void captureTelemetry(CloudProtocol::Sensor (&samples)[6],bool& paused,uint64_t nowMs) {
  paused=setupPaused;
  for(uint8_t i=0;i<6;i++) {
    const Reading& r=readings[i];
    auto& sample=samples[i];
    sample.value=r.value; // Already converted by the existing drivers; never rescale.
    sample.ever=r.ever;
    sample.ageMs=r.ever&&nowMs>=r.cloudLastOk?nowMs-r.cloudLastOk:0;
    sample.valid=(i==5||settings.sensors[i].enabled)&&r.valid&&r.ever&&nowMs>=r.cloudLastOk&&sample.ageMs<STALE_MS&&!(i<5&&setupPaused);
  }
}
String statusJson() {
  uint32_t now=millis();bool connected=WiFi.status()==WL_CONNECTED;
  String j="{\"firmware\":\"3.0.3-preview.1\",\"revision\":"+String(configRevision)+",\"setupPaused\":"+(setupPaused?"true":"false")+",\"uptime\":"+String(now/1000)+",\"wifi\":"+(connected?"true":"false")+",\"ip\":"+quote(connected?WiFi.localIP().toString():String(""))+
    ",\"ap\":"+(apStarted?"true":"false")+",\"rssi\":"+String(connected?WiFi.RSSI():0)+",\"sensors\":[";
  for(uint8_t i=0;i<6;i++){
    if(i)j+=',';auto &r=readings[i];bool enabled=i==5||settings.sensors[i].enabled;
    bool fresh=r.valid&&!elapsed(now,r.lastOk,STALE_MS)&&!(i<5&&setupPaused);
    String state=!enabled?"Run guided setup":(i<5&&setupPaused)?"Setup mode: polling paused":fresh?"Live":r.valid?"Stale":r.error;
    j+="{\"name\":"+quote(names[i])+",\"unit\":"+quote(units[i])+",\"status\":"+quote(state)+",\"valid\":"+(fresh&&enabled?"true":"false")+",\"value\":"+(fresh&&enabled?String(r.value,3):String("null"))+
      ",\"age\":"+(r.ever?String(uint32_t(now-r.lastOk)/1000):String("null"))+",\"good\":"+String(r.good)+",\"bad\":"+String(r.bad)+",\"raw\":"+quote(r.raw)+"}";
  }
  j+="],\"setup\":{\"active\":"+String(commission.active()?"true":"false")+",\"sensor\":"+String(commission.sensor)+",\"baud\":"+String(commission.currentBaud)+",\"targetBaud\":"+String(commission.targetBaud)+
    ",\"saved\":"+(commissionSaved?"true":"false")+",\"writeSent\":"+(commission.writeSent?"true":"false")+
    ",\"message\":"+quote(commissionSaved?commissionResult:String(commission.message()))+
    ",\"tx\":"+quote(commissionTx)+",\"rx\":"+quote(commissionRx)+",\"transport\":"+quote(commissionTransport)+"}";
  j+=",\"probe\":{\"number\":"+String(probe.number)+",\"state\":"+quote(probe.state)+",\"tx\":"+quote(probe.tx)+",\"rx\":"+quote(probe.rx)+",\"error\":"+quote(probe.error)+",\"values\":"+quote(probe.values)+"}}";
  return j;
}
void saveBusHandler() {
  if(!authorized())return;if(tr.active||commission.active()){bad("Bus busy; wait for setup or the reading to finish",409);return;}
  uint32_t i,baud,framing,automatic;
  if(!integerArg("bus",0,1,i)||!integerArg("baud",1200,115200,baud)||!integerArg("framing",0,3,framing)||!integerArg("automatic",0,1,automatic)){bad("Invalid bus settings");return;}
  Settings candidate=settings;candidate.buses[i]={baud,uint8_t(framing),bool(automatic)};
  if(!validSettings(candidate)){bad("Unsupported bus settings");return;}
  if(!saveSettings(candidate)){bad("Could not save settings",500);return;}
  beginBus(i);for(uint8_t s=0;s<5;s++)if(sensorBus[s]==i){readings[s].valid=false;readings[s].error="Bus settings changed; waiting";}
  sendJson("{\"ok\":true}");
}
void saveSensorHandler() {
  if(!authorized())return;if(tr.active||commission.active()){bad("Bus busy; wait for setup or the reading to finish",409);return;}
  uint32_t i,id,fn,reg,format,enabled,swap,normal,alarm;double scale,offset;
  if(!integerArg("sensor",0,4,i)||!integerArg("enabled",0,1,enabled)||!integerArg("id",1,247,id)||!integerArg("fn",1,4,fn)||!integerArg("reg",0,65535,reg)||!integerArg("format",0,7,format)||!integerArg("swapBytes",0,1,swap)||!integerArg("normalRaw",0,UINT32_MAX,normal)||!integerArg("alarmRaw",0,UINT32_MAX,alarm)||!doubleArg("scale",scale)||!doubleArg("offset",offset)){bad("Complete every field with valid numbers (decimal or 0x hex addresses)");return;}
  Settings candidate=settings;auto &c=candidate.sensors[i];c.enabled=enabled;c.id=id;c.fn=fn;c.reg=reg;c.format=format;c.swapBytes=swap;c.scale=scale;c.offset=offset;c.normalRaw=normal;c.alarmRaw=alarm;
  if(!validSettings(candidate)){bad("Check format, scale, status codes and unique IDs on this bus");return;}
  if(!saveSettings(candidate)){bad("Could not save settings",500);return;}
  readings[i]=Reading();readings[i].error=enabled?"Waiting for first reply":"Needs setup";
  sendJson("{\"ok\":true}");
}
void probeHandler() {
  if(!authorized())return;if(commission.active()){bad("Wait for guided setup to finish",409);return;}uint32_t bus,id,fn,reg,count;
  if(!integerArg("bus",0,1,bus)||!integerArg("id",1,247,id)||!integerArg("fn",1,4,fn)||!integerArg("reg",0,65535,reg)||!integerArg("count",1,8,count)||!Modbus::validRequest(id,fn,reg,count)){bad("Invalid read request");return;}
  if(!startTransaction(bus,id,fn,reg,count,-1)){bad("Bus busy; try Read again",409);return;}
  sendJson("{\"ok\":true}",202);
}
void pollingHandler() {
  if(!authorized())return;
  uint32_t pause,confirmed=0;
  if(!integerArg("pause",0,1,pause)){bad("Invalid polling request");return;}
  if(commission.active()){bad("Wait for guided setup to finish",409);return;}
  if(!pause&&(!integerArg("reconnected",1,1,confirmed))){bad("Confirm only the configured sensors are reconnected to their correct buses");return;}
  if(!prefsReady||prefs.putBool("setupPaused",pause)!=1){bad("Could not save polling mode",500);return;}
  setupPaused=pause;
  for(uint8_t i=0;i<5;i++){readings[i].valid=false;readings[i].error=setupPaused?"Setup mode: polling paused":"Waiting for first reply";}
  sendJson("{\"ok\":true}");
}
void commissionHandler() {
  if(!authorized())return;
  uint32_t sensor,id,isolated;
  if(!integerArg("sensor",0,4,sensor)||!Commission::supported(sensor)||!integerArg("currentId",1,247,id)||!integerArg("isolated",1,1,isolated)){
    bad("Select a supported sensor, enter its current ID, and confirm it is the only sensor connected to that bus");return;
  }
  if(!setupPaused){bad("Pause RS485 polling before changing connections");return;}
  if(tr.active||commission.active()){bad("Bus busy; wait for the current operation",409);return;}
  auto p=Commission::profile(sensor);auto b=settings.buses[p.bus];
  if(!Commission::commonBaud(b.baud)||(p.bus==1&&b.baud!=9600)||b.framing!=0){bad("Guided setup needs 8N1, outdoor 4800 or 9600, indoor 9600. Correct Advanced bus settings first.");return;}
  Settings candidate=settings;candidate.sensors[sensor]=documentedProfile(sensor);if(sensor==1)candidate.sensors[sensor].id=id;candidate.sensors[sensor].enabled=true;
  if(!validSettings(candidate)){bad("Another enabled profile uses this target ID. Disable that conflicting profile in Advanced settings first.");return;}
  candidate.sensors[sensor].enabled=false;
  if(!saveSettings(candidate)){bad("Could not save setup state; no sensor write was sent",500);return;}
  readings[sensor]=Reading();
  commissionSaved=false;commissionRestored=false;commissionTx="";commissionRx="";commissionTransport="";commissionResult="";
  commission.start(sensor,id,b.baud);
  sendJson("{\"ok\":true}",202);
}
void startServer() {
  const char *headers[]={"X-SkyMind-Token"};server.collectHeaders(headers,1);
  server.on("/",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.sendHeader("X-Content-Type-Options","nosniff");server.send_P(200,"text/html; charset=utf-8",DASHBOARD_HTML);});
  server.on("/api/config",HTTP_GET,[]{sendJson(configJson());});
  server.on("/api/status",HTTP_GET,[]{sendJson(statusJson());});
  server.on("/api/bus",HTTP_POST,saveBusHandler);
  server.on("/api/sensor",HTTP_POST,saveSensorHandler);
  server.on("/api/probe",HTTP_POST,probeHandler);
  server.on("/api/commission",HTTP_POST,commissionHandler);
  server.on("/api/polling",HTTP_POST,pollingHandler);
  server.onNotFound([]{bad("Not found",404);});server.begin();
}
}

void setupApp() {
  Serial.begin(115200);delay(150);
  pinMode(STATUS_LED_PIN,OUTPUT);setWifiPhase(WifiPhase::Disconnected);
  Serial.println("\nSkyMind environmental monitor 3.0.3-preview.1 — classic ESP32-WROOM");
  defaults();prefsReady=prefs.begin("skymind-env",false);
  if(prefsReady&&prefs.getBytesLength("settings")==sizeof settings){Settings stored={};prefs.getBytes("settings",&stored,sizeof stored);if(validSettings(stored))settings=stored;}
  // Keep the v1 storage layout and preserve already-enabled custom profiles.
  // On the first v2 boot, populate disabled cards with the documented maps.
  if(prefsReady&&!prefs.getBool("profilesV2",false)){
    Settings candidate=settings;
    for(uint8_t i=0;i<5;i++)if(!candidate.sensors[i].enabled)candidate.sensors[i]=documentedProfile(i);
    if(saveSettings(candidate))prefs.putBool("profilesV2",true);
  }
  setupPaused=!prefsReady||prefs.getBool("setupPaused",true);
  for(uint8_t i=0;i<5;i++)if(settings.sensors[i].enabled)readings[i].error="Waiting for first reply";
  beginBus(0);beginBus(1);scanTemperature();
  char id[7],secret[17],token[33];snprintf(id,sizeof id,"%06X",uint32_t(ESP.getEfuseMac())&0xffffff);
  snprintf(secret,sizeof secret,"%08lX%08lX",(unsigned long)esp_random(),(unsigned long)esp_random());
  snprintf(token,sizeof token,"%08lX%08lX%08lX%08lX",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
  csrf=token;apName=String("SkyMind-Env-")+id;apPassword=secret;
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setHostname(MDNS_NAME);WiFi.setSleep(false);WiFi.setAutoReconnect(false);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  setWifiPhase(WifiPhase::Connecting);
  Serial.println("Connecting to configured ZTE Wi-Fi (2.4 GHz)...");wifiLostSince=millis();
  startServer();
  telemetryBegin();
}
void loopApp() {
  serviceTransaction();serviceTemperature();serviceWifi();serviceLed();server.handleClient();serviceCommission();pollSensors();telemetryTick(captureTelemetry);delay(1);
}
