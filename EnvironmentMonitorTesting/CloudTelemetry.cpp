#include "CloudTelemetry.h"
#include "CloudConfig.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <esp_sntp.h>
#include <esp_crt_bundle.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

namespace {
struct Snapshot { char json[2048]; uint64_t capturedUptimeMs; };
struct Shared {
  bool synced=false,stopped=false,resync=false;
  uint64_t syncAt=0,syncEpoch=0,nextAttempt=0;
} state;
portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t pending=nullptr;
char bootId[37]={};
uint64_t bootNumber=0,sequence=0,lastPublication=0,lastCaptureEpoch=0,lastNtpStart=0;
bool wasConnected=false,clockReadyLogged=false;
String caPem;
Shared readState(){portENTER_CRITICAL(&stateMux);Shared copy=state;portEXIT_CRITICAL(&stateMux);return copy;}
int64_t epochMs(){struct timeval tv;gettimeofday(&tv,nullptr);return int64_t(tv.tv_sec)*1000+tv.tv_usec/1000;}
void onTimeSync(struct timeval* tv) {
  const uint64_t now=telemetryUptimeMs();
  const int64_t epoch=int64_t(tv->tv_sec)*1000+tv->tv_usec/1000;
  portENTER_CRITICAL(&stateMux);
  state.synced=epoch>=1704067200000LL;state.syncAt=now;state.syncEpoch=epoch>0?uint64_t(epoch):0;
  portEXIT_CRITICAL(&stateMux);
}
void beginClockSync(uint64_t now) {
  lastNtpStart=now;
  Serial.println("[cloud] Requesting UTC synchronization; telemetry waits for valid time");
  esp_sntp_set_time_sync_notification_cb(onTimeSync);
  configTime(0,0,CLOUD_NTP_SERVER_1,CLOUD_NTP_SERVER_2,CLOUD_NTP_SERVER_3);
  esp_sntp_set_sync_interval(15*60*1000);
}
bool configured() {
  if(!CLOUD_TELEMETRY_ENABLED)return false;
  if(strcmp(CLOUD_TELEMETRY_URL,"https://environment-dashboard.skymindapp.pages.dev/api/xaerova/environment/ingest")) {
    Serial.println("[cloud] Wrong preview endpoint; review CloudConfig.h");return false;
  }
  if(!CloudProtocol::deviceIdValid(CLOUD_DEVICE_ID)||!strcmp(CLOUD_DEVICE_ID,"REPLACE_WITH_DOCK_DEVICE_ID")||!CloudProtocol::credentialValid(CLOUD_TELEMETRY_TOKEN)) {
    Serial.println("[cloud] Fill the provisioned device ID/credential in private DeviceSecrets.h");return false;
  }
  caPem=CLOUD_TLS_CA_CERT;caPem.trim();
  if(caPem.length())return caPem.startsWith("-----BEGIN CERTIFICATE-----");
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  return true;
#else
  Serial.println("[cloud] This core has no CA bundle; supply a trusted PEM CA");return false;
#endif
}
int publish(const Snapshot& snapshot) {
  // Cooperative async HTTPS; local sensor polling stays on the original Arduino loop.
  // No redirects, no insecure TLS fallback, no shared access to sensor/UART objects.
  esp_http_client_config_t config={};
  config.url=CLOUD_TELEMETRY_URL;config.method=HTTP_METHOD_POST;
  config.timeout_ms=500;config.is_async=true;config.disable_auto_redirect=true;
  config.skip_cert_common_name_check=false;config.keep_alive_enable=false;
  config.buffer_size=1024;config.buffer_size_tx=1024;
  if(caPem.length())config.cert_pem=caPem.c_str();
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  else config.crt_bundle_attach=esp_crt_bundle_attach;
#endif
  esp_http_client_handle_t client=esp_http_client_init(&config);
  if(!client){Serial.println("[cloud] HTTPS client allocation failed");return -1;}
  String auth=String("Bearer ")+CLOUD_TELEMETRY_TOKEN;
  esp_http_client_set_header(client,"Authorization",auth.c_str());
  esp_http_client_set_header(client,"Content-Type","application/json");
  esp_http_client_set_post_field(client,snapshot.json,strlen(snapshot.json));
  const uint64_t started=telemetryUptimeMs();
  esp_err_t result=ESP_ERR_HTTP_EAGAIN;
  while(result==ESP_ERR_HTTP_EAGAIN&&telemetryUptimeMs()-started<CloudProtocol::REQUEST_DEADLINE_MS&&WiFi.status()==WL_CONNECTED) {
    result=esp_http_client_perform(client);
    if(result==ESP_ERR_HTTP_EAGAIN)vTaskDelay(pdMS_TO_TICKS(10));
  }
  int status=result==ESP_OK?esp_http_client_get_status_code(client):-1;
  if(result!=ESP_OK)Serial.printf("[cloud] HTTPS request failed: %s; check Wi-Fi/DNS/TLS/UTC or request deadline\n",esp_err_to_name(result));
  esp_http_client_cleanup(client);return status;
}
void sender(void*) {
  Snapshot snapshot;unsigned failures=0,conflicts=0;
  uint64_t lastSuccessLog=0;bool websiteConfirmed=false;
  for(;;) {
    if(xQueueReceive(pending,&snapshot,pdMS_TO_TICKS(250))!=pdTRUE)continue;
    const Shared before=readState();const uint64_t now=telemetryUptimeMs();
    if(before.stopped||WiFi.status()!=WL_CONNECTED||now<before.nextAttempt||!CloudProtocol::pendingFresh(snapshot.capturedUptimeMs,now))continue;
    const int status=publish(snapshot);const uint64_t finished=telemetryUptimeMs();
    if(status>=200&&status<300) {
      if(!websiteConfirmed||failures||finished-lastSuccessLog>=60000) {
        Serial.printf("[cloud] Website accepted telemetry: HTTP %d (HTTPS verified)\n",status);
        lastSuccessLog=finished;websiteConfirmed=true;
      }
      failures=conflicts=0;
      portENTER_CRITICAL(&stateMux);state.nextAttempt=finished;portEXIT_CRITICAL(&stateMux);
      continue;
    }
    ++failures;conflicts=status==409?conflicts+1:0;
    const bool fatal=(status>=300&&status<500&&status!=409&&status!=422&&status!=429)||conflicts>=3;
    portENTER_CRITICAL(&stateMux);
    state.nextAttempt=finished+CloudProtocol::retryMs(failures);
    if(status==422){state.synced=false;state.resync=true;}
    if(fatal)state.stopped=true;
    portEXIT_CRITICAL(&stateMux);
    // Never print credential, Authorization header, Wi-Fi details or full payload.
    Serial.printf("[cloud] HTTP %d%s\n",status,fatal?"; stopped, check provisioning/payload":"; retrying with a new current snapshot");
  }
}
}

uint64_t telemetryUptimeMs(){return uint64_t(esp_timer_get_time())/1000;}
void telemetryBegin() {
  if(!configured()) {Serial.println("[cloud] Publishing disabled/unconfigured; local sensors remain active");return;}
  // This namespace is separate from the commissioned skymind-env structure/keys.
  Preferences cloud;
  if(!cloud.begin("skymind-cloud",false)){Serial.println("[cloud] Cannot open cloud counter storage; publishing disabled");return;}
  if(cloud.isKey("bootNumber")&&cloud.getType("bootNumber")!=PT_U64) {
    cloud.end();Serial.println("[cloud] Invalid cloud counter storage; publishing disabled");return;
  }
  uint64_t previous=cloud.getULong64("bootNumber",0);
  if(previous>=CloudProtocol::MAX_INTEGER||cloud.putULong64("bootNumber",previous+1)!=sizeof(uint64_t)) {
    cloud.end();Serial.println("[cloud] Cannot persist boot counter; publishing disabled");return;
  }
  bootNumber=previous+1;cloud.end();
  uint8_t random[16];esp_fill_random(random,sizeof random);CloudProtocol::uuid(bootId,random);
  pending=xQueueCreate(1,sizeof(Snapshot));
  if(!pending){Serial.println("[cloud] Queue allocation failed; publishing disabled");return;}
  if(xTaskCreatePinnedToCore(sender,"cloud-sender",12288,nullptr,1,nullptr,0)!=pdPASS) {
    vQueueDelete(pending);pending=nullptr;Serial.println("[cloud] Task allocation failed; publishing disabled");return;
  }
  beginClockSync(telemetryUptimeMs());
  Serial.println("[cloud] Configured; waiting for internet and synchronized UTC");
}
void telemetryTick(CaptureTelemetry capture) {
  if(!pending)return;
  const uint64_t now=telemetryUptimeMs();
  const bool connected=WiFi.status()==WL_CONNECTED;
  if(!connected){clockReadyLogged=false;wasConnected=false;xQueueReset(pending);return;}
  if(!wasConnected) {
    wasConnected=true;lastPublication=now>=CLOUD_TELEMETRY_INTERVAL_MS?now-CLOUD_TELEMETRY_INTERVAL_MS:0;
    portENTER_CRITICAL(&stateMux);state.nextAttempt=0;portEXIT_CRITICAL(&stateMux);
  }
  Shared current=readState();if(current.stopped)return;
  if(current.resync) {
    portENTER_CRITICAL(&stateMux);state.resync=false;portEXIT_CRITICAL(&stateMux);
    beginClockSync(now);current=readState();
  }
  const int64_t wall=epochMs();
  if(!CloudProtocol::clockUsable(current.synced,now,current.syncAt,wall)) {
    clockReadyLogged=false;
    if(now-lastNtpStart>=30000)beginClockSync(now);
    return;
  }
  // Withhold on clock steps or backward capture time; never re-date old samples.
  const int64_t expected=int64_t(current.syncEpoch)+int64_t(now-current.syncAt);
  if(wall-expected>1000||expected-wall>1000) {
    portENTER_CRITICAL(&stateMux);state.synced=false;state.resync=true;portEXIT_CRITICAL(&stateMux);return;
  }
  if(!clockReadyLogged){Serial.println("[cloud] UTC synchronized; ready for verified HTTPS telemetry");clockReadyLogged=true;}
  if(uint64_t(wall)<lastCaptureEpoch||now<current.nextAttempt||sequence>CloudProtocol::MAX_INTEGER)return;
  if(lastPublication&&now-lastPublication<CLOUD_TELEMETRY_INTERVAL_MS)return;
  CloudProtocol::Sensor samples[6];bool paused=false;capture(samples,paused,now);
  CloudProtocol::Meta meta{CLOUD_DEVICE_ID,bootId,bootNumber,sequence,now,uint64_t(wall),paused};
  Snapshot snapshot={};snapshot.capturedUptimeMs=now;
  if(!CloudProtocol::encode(snapshot.json,sizeof snapshot.json,meta,samples)) {
    Serial.println("[cloud] Snapshot encoding failed; no telemetry sent");return;
  }
  ++sequence;lastPublication=now;lastCaptureEpoch=uint64_t(wall);
  xQueueOverwrite(pending,&snapshot); // Latest only; never queue an old backlog.
}
