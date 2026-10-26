#pragma once
// Portable wire-format/policy code shared by the firmware and host contract tests.
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
namespace CloudProtocol {
constexpr uint64_t MAX_INTEGER=9007199254740991ULL;
constexpr uint64_t STALE_MS=10000, CLOCK_ERROR_MS=2000;
constexpr uint64_t SYNC_MAX_AGE_MS=1800000, PENDING_MAX_AGE_MS=500, REQUEST_DEADLINE_MS=3500;
constexpr const char* FIRMWARE="3.0.3-preview.1";
struct Sensor { double value=0; bool valid=false, ever=false; uint64_t ageMs=0; };
struct Meta {
  const char* deviceId; const char* bootId;
  uint64_t bootNumber,sequence,uptimeMs,capturedAt; bool paused;
};
inline bool deviceIdValid(const char* id) {
  const size_t n=strlen(id);if(!n||n>128)return false;
  for(size_t i=0;i<n;i++){const char c=id[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c==':'||c=='-'))return false;}
  return true;
}
inline bool credentialValid(const char* token) {
  if(strlen(token)!=69||strncmp(token,"xenv_",5))return false;
  for(size_t i=5;i<69;i++)if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f')))return false;
  return true;
}
inline bool uuidValid(const char* id) {
  if(strlen(id)!=36||id[14]!='4'||!(id[19]=='8'||id[19]=='9'||id[19]=='a'||id[19]=='b'))return false;
  for(size_t i=0;i<36;i++){if(i==8||i==13||i==18||i==23){if(id[i]!='-')return false;}else if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return false;}
  return true;
}
inline void uuid(char (&out)[37],const uint8_t (&random)[16]) {
  uint8_t bytes[16];memcpy(bytes,random,16);bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;
  snprintf(out,sizeof out,"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
}
inline bool clockUsable(bool synced,uint64_t nowMs,uint64_t syncMs,int64_t epochMs) {
  return synced&&nowMs>=syncMs&&nowMs-syncMs<=SYNC_MAX_AGE_MS&&epochMs>=1704067200000LL&&uint64_t(epochMs)<=MAX_INTEGER;
}
inline bool pendingFresh(uint64_t capturedUptime,uint64_t nowMs) {return nowMs>=capturedUptime&&nowMs-capturedUptime<=PENDING_MAX_AGE_MS;}
inline uint32_t retryMs(unsigned failures) {return failures>=5?30000U:(2000U << (failures?failures-1:0));}
inline bool sensorValid(size_t i,const Sensor& s,uint64_t uptime,bool paused) {
  if(!s.valid||!s.ever||s.ageMs>uptime||s.ageMs>=STALE_MS||!isfinite(s.value)||(paused&&i<5))return false;
  if(i==1||i==4)return s.value==0||s.value==1;
  if(i==0||i==2)return s.value>=0;
  if(i==3)return s.value>=0&&s.value<=100;
  return s.value>=-55&&s.value<=125;
}
struct Writer {
  char* out;size_t size,used=0;bool ok=true;
  void add(const char* format,...) {
    if(!ok)return;va_list args;va_start(args,format);int n=vsnprintf(out+used,size-used,format,args);va_end(args);
    if(n<0||size_t(n)>=size-used){ok=false;return;}used+=size_t(n);
  }
};
inline bool encode(char* output,size_t capacity,const Meta& m,const Sensor (&sensors)[6]) {
  if(!capacity||!deviceIdValid(m.deviceId)||!uuidValid(m.bootId)||!m.bootNumber||m.bootNumber>MAX_INTEGER||m.sequence>MAX_INTEGER||m.uptimeMs>MAX_INTEGER||m.capturedAt>MAX_INTEGER)return false;
  Writer w{output,capacity};
  w.add("{\"schemaVersion\":1,\"deviceId\":\"%s\",\"firmware\":\"%s\",\"bootId\":\"%s\",\"bootNumber\":%llu,\"sequence\":%llu,\"uptimeMs\":%llu,\"capturedAt\":%llu,\"setupPaused\":%s,\"sensors\":{",m.deviceId,FIRMWARE,m.bootId,(unsigned long long)m.bootNumber,(unsigned long long)m.sequence,(unsigned long long)m.uptimeMs,(unsigned long long)m.capturedAt,m.paused?"true":"false");
  const char* keys[]={"wind","rain","light","humidity","water_ingress","temperature"};
  for(size_t i=0;i<6;i++) {
    const auto& s=sensors[i];bool valid=sensorValid(i,s,m.uptimeMs,m.paused);
    w.add("%s\"%s\":{\"valid\":%s,\"value\":",i?",":"",keys[i],valid?"true":"false");
    if(valid)w.add("%.6f",s.value);else w.add("null");
    w.add(",\"ageMs\":");
    if(s.ever&&s.ageMs<=m.uptimeMs)w.add("%llu",(unsigned long long)s.ageMs);else w.add("null");
    w.add("}");
  }
  w.add("}}");return w.ok;
}
}
