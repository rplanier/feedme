#include "ota_server.h"
#include "wifi_manager.h"
#include <Update.h>

OTAServer otaServer;

// Minimal upload form, served as a fallback for updating from a browser when
// the app is unavailable. Kept small since it lives in flash as a literal.
static const char UPLOAD_PAGE[] PROGMEM = R"HTML(<!doctype html>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>FeedMe Firmware Update</title>
<style>
body{font:16px -apple-system,sans-serif;margin:0;padding:24px;background:#1c1c1e;color:#fff}
h1{font-size:20px;margin:0 0 4px}p{color:#98989f;margin:0 0 24px}
input,button{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:0;font-size:16px}
input{background:#2c2c2e;color:#fff;margin-bottom:12px}
button{background:#0a84ff;color:#fff;font-weight:600}button:disabled{opacity:.4}
#s{margin-top:16px;text-align:center}
</style>
<h1>FeedMe Firmware Update</h1>
<p>Select a .bin firmware image.</p>
<input type=file id=f accept=".bin">
<button id=b onclick=up()>Start Update</button>
<div id=s></div>
<script>
function up(){
  var f=document.getElementById('f').files[0];if(!f){return}
  var d=new FormData();d.append('firmware',f);
  var x=new XMLHttpRequest();x.open('POST','/update');
  document.getElementById('b').disabled=true;
  x.upload.onprogress=function(e){
    document.getElementById('s').textContent='Uploading '+Math.round(e.loaded/e.total*100)+'%'};
  x.onload=function(){document.getElementById('s').textContent=
    x.status==200?'Update complete. Device is rebooting.':'Failed: '+x.responseText};
  x.onerror=function(){document.getElementById('s').textContent='Connection lost'};
  x.send(d);
}
</script>
)HTML";

void OTAServer::begin() {
    if (initialized) {
        return;
    }
    registerRoutes();
    initialized = true;
    DEBUG_PRINTLN("OTA: Server initialized");
}

void OTAServer::start() {
    if (!initialized || running) {
        return;
    }
    server.begin();
    running = true;
    state = State::IDLE;
    bytesWritten = 0;
    totalBytes = 0;
    errorMessage[0] = '\0';
    DEBUG_PRINTF("OTA: Server listening on port %u\n", OTA_PORT);
}

void OTAServer::stop() {
    if (!running) {
        return;
    }

    // Abandon any partially written image so the next attempt starts clean
    if (state == State::UPLOADING) {
        Update.abort();
        state = State::FAILED;
        DEBUG_PRINTLN("OTA: Server stopped mid-upload, update aborted");
    }

    server.end();
    running = false;
    rebootAt = 0;
    DEBUG_PRINTLN("OTA: Server stopped");
}

void OTAServer::update() {
    if (rebootAt != 0 && millis() >= rebootAt) {
        DEBUG_PRINTLN("OTA: Rebooting into new firmware");
        Serial.flush();
        ESP.restart();
    }
}

uint8_t OTAServer::getProgressPercent() const {
    if (totalBytes == 0) {
        return 0;
    }
    size_t pct = (bytesWritten * 100) / totalBytes;
    return pct > 100 ? 100 : (uint8_t)pct;
}

void OTAServer::failUpload(const char* message) {
    strncpy(errorMessage, message, sizeof(errorMessage) - 1);
    errorMessage[sizeof(errorMessage) - 1] = '\0';
    state = State::FAILED;
    DEBUG_PRINTF("OTA: %s\n", errorMessage);
}

void OTAServer::handleUploadChunk(AsyncWebServerRequest* request, size_t index,
                                  uint8_t* data, size_t len, bool final) {
    // A long upload must not trip the WiFi idle timeout mid-transfer
    wifiManager.resetIdleTimer();

    if (index == 0) {
        totalBytes = request->contentLength();
        bytesWritten = 0;
        errorMessage[0] = '\0';
        state = State::UPLOADING;
        DEBUG_PRINTF("OTA: Upload started (%u bytes)\n", (unsigned)totalBytes);

        // Every ESP32 app image starts with 0xE9. Anything else means the
        // uploader sent the wrong file or a malformed multipart body, and the
        // Update library would otherwise report it as a "Decryption error".
        if (len == 0 || data[0] != 0xE9) {
            failUpload("Not an ESP32 firmware image (bad header)");
            return;
        }

        // UPDATE_SIZE_UNKNOWN sizes the write to the whole inactive OTA slot;
        // this fails outright if the partition table has no second app slot
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            failUpload(Update.errorString());
            return;
        }
    }

    if (state != State::UPLOADING) {
        return;  // Already failed; swallow the rest of the body
    }

    if (Update.write(data, len) != len) {
        failUpload(Update.errorString());
        Update.abort();
        return;
    }
    bytesWritten += len;

    if (final) {
        if (!Update.end(true)) {
            failUpload(Update.errorString());
            return;
        }
        state = State::SUCCESS;
        DEBUG_PRINTF("OTA: Upload complete (%u bytes)\n", (unsigned)bytesWritten);
    }
}

void OTAServer::registerRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", UPLOAD_PAGE);
    });

    server.on("/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        const char* stateName = "idle";
        switch (state) {
            case State::UPLOADING: stateName = "uploading"; break;
            case State::SUCCESS:   stateName = "success";   break;
            case State::FAILED:    stateName = "failed";    break;
            case State::IDLE:      stateName = "idle";      break;
        }

        char body[192];
        snprintf(body, sizeof(body),
                 "{\"version\":\"%s\",\"state\":\"%s\",\"progress\":%u,\"error\":\"%s\"}",
                 FEEDME_VERSION, stateName, getProgressPercent(), errorMessage);
        request->send(200, "application/json", body);
    });

    server.on(
        "/update", HTTP_POST,
        // Fires once the whole body has been received
        [this](AsyncWebServerRequest* request) {
            if (state == State::SUCCESS) {
                request->send(200, "application/json",
                              "{\"status\":\"success\","
                              "\"message\":\"Update complete, rebooting...\"}");
                // Defer the restart so this response can flush first
                rebootAt = millis() + REBOOT_DELAY_MS;
            } else {
                char body[128];
                snprintf(body, sizeof(body),
                         "{\"status\":\"error\",\"message\":\"%s\"}",
                         errorMessage[0] ? errorMessage : "Upload failed");
                request->send(400, "application/json", body);
            }
        },
        // Fires per chunk as the body streams in
        [this](AsyncWebServerRequest* request, const String& filename, size_t index,
               uint8_t* data, size_t len, bool final) {
            (void)filename;
            handleUploadChunk(request, index, data, len, final);
        });

    server.onNotFound([](AsyncWebServerRequest* request) {
        // Captive-portal friendly: send stray requests to the upload page
        request->redirect("/");
    });
}
