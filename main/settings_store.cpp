#include "settings_store.hpp"
#include <cstring>
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

bool SettingsStore::begin() {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); e = nvs_flash_init();
    }
    return e == ESP_OK;
}

static std::string get(nvs_handle_t h, const char* key) {
    size_t n = 0;
    if (nvs_get_str(h, key, nullptr, &n) != ESP_OK || n < 1) return {};
    std::string s(n, '\0');
    if (nvs_get_str(h, key, s.data(), &n) != ESP_OK) return {};
    s.resize(n - 1); return s;
}

static bool getExisting(nvs_handle_t h, const char* key, std::string& value) {
    size_t n=0;if(nvs_get_str(h,key,nullptr,&n)!=ESP_OK||n<1)return false;
    std::string stored(n,'\0');if(nvs_get_str(h,key,stored.data(),&n)!=ESP_OK)return false;
    stored.resize(n-1);value=std::move(stored);return true;
}

namespace {
const char* typeName(SensorType t) {
    switch (t) {
        case SensorType::ILLUMINANCE: return "illuminance";
        case SensorType::ENV: return "env";
        case SensorType::ENOCEAN: return "enocean";
        case SensorType::XIAOMI_S400: return "xiaomi_s400";
    }
    return "illuminance";
}
bool typeFromName(const char* s, SensorType& t) {
    if (!s) return false;
    if (!strcmp(s, "illuminance")) t = SensorType::ILLUMINANCE;
    else if (!strcmp(s, "env")) t = SensorType::ENV;
    else if (!strcmp(s, "enocean")) t = SensorType::ENOCEAN;
    else if (!strcmp(s, "xiaomi_s400")) t = SensorType::XIAOMI_S400;
    else return false;
    return true;
}
bool parseMac(const std::string& text, std::array<uint8_t,6>& mac) {
    unsigned v[6];
    if (std::sscanf(text.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)v[i];
    return true;
}
std::string macToString(const std::array<uint8_t,6>& mac) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}
std::string sensorsToJson(const std::vector<SensorDevice>& sensors) {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& d : sensors) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", d.name.c_str());
        cJSON_AddStringToObject(o, "mac", macToString(d.mac).c_str());
        cJSON_AddStringToObject(o, "type", typeName(d.type));
        cJSON_AddNumberToObject(o, "rate_ms", (double)d.rate_limit_ms);
        cJSON* chans = cJSON_CreateArray();
        for (const auto& ch : d.channels) {
            cJSON* co = cJSON_CreateObject();
            cJSON_AddStringToObject(co, "index", ch.index.c_str());
            cJSON_AddStringToObject(co, "field", ch.field.c_str());
            cJSON_AddItemToArray(chans, co);
        }
        cJSON_AddItemToObject(o, "channels", chans);
        if (!d.index.empty()) cJSON_AddStringToObject(o, "index", d.index.c_str());
        if (!d.bindkey.empty()) cJSON_AddStringToObject(o, "bindkey", d.bindkey.c_str());
        if (d.height_cm != 0.0f) cJSON_AddNumberToObject(o, "height_cm", (double)d.height_cm);
        if (!d.birth_date.empty()) cJSON_AddStringToObject(o, "birth_date", d.birth_date.c_str());
        if (d.body_fat_offset != 0.0f) cJSON_AddNumberToObject(o, "fat_offset", (double)d.body_fat_offset);
        cJSON_AddItemToArray(arr, o);
    }
    char* s = cJSON_PrintUnformatted(arr);
    std::string out = s ? s : "[]";
    if (s) cJSON_free(s);
    cJSON_Delete(arr);
    return out;
}
void jsonToSensors(const std::string& text, std::vector<SensorDevice>& sensors) {
    sensors.clear();
    if (text.empty()) return;
    cJSON* arr = cJSON_Parse(text.c_str());
    if (!cJSON_IsArray(arr)) { if (arr) cJSON_Delete(arr); return; }
    cJSON* o = nullptr;
    cJSON_ArrayForEach(o, arr) {
        if (sensors.size() >= MAX_SENSORS) break;
        SensorDevice d;
        if (cJSON_IsString(cJSON_GetObjectItem(o, "name"))) d.name = cJSON_GetObjectItem(o, "name")->valuestring;
        if (cJSON_IsString(cJSON_GetObjectItem(o, "mac"))) parseMac(cJSON_GetObjectItem(o, "mac")->valuestring, d.mac);
        if (cJSON_IsString(cJSON_GetObjectItem(o, "type"))) typeFromName(cJSON_GetObjectItem(o, "type")->valuestring, d.type);
        if (cJSON_IsNumber(cJSON_GetObjectItem(o, "rate_ms"))) d.rate_limit_ms = (uint32_t)cJSON_GetObjectItem(o, "rate_ms")->valuedouble;
        cJSON* chans = cJSON_GetObjectItem(o, "channels");
        if (cJSON_IsArray(chans)) {
            cJSON* co = nullptr;
            cJSON_ArrayForEach(co, chans) {
                if (d.channels.size() >= MAX_CHANNELS) break;
                SensorChannel ch;
                if (cJSON_IsString(cJSON_GetObjectItem(co, "index"))) ch.index = cJSON_GetObjectItem(co, "index")->valuestring;
                if (cJSON_IsString(cJSON_GetObjectItem(co, "field"))) ch.field = cJSON_GetObjectItem(co, "field")->valuestring;
                d.channels.push_back(std::move(ch));
            }
        }
        if (cJSON_IsString(cJSON_GetObjectItem(o, "index"))) d.index = cJSON_GetObjectItem(o, "index")->valuestring;
        if (cJSON_IsString(cJSON_GetObjectItem(o, "bindkey"))) d.bindkey = cJSON_GetObjectItem(o, "bindkey")->valuestring;
        if (cJSON_IsNumber(cJSON_GetObjectItem(o, "height_cm"))) d.height_cm = (float)cJSON_GetObjectItem(o, "height_cm")->valuedouble;
        if (cJSON_IsString(cJSON_GetObjectItem(o, "birth_date"))) d.birth_date = cJSON_GetObjectItem(o, "birth_date")->valuestring;
        if (cJSON_IsNumber(cJSON_GetObjectItem(o, "fat_offset"))) d.body_fat_offset = cJSON_GetObjectItem(o, "fat_offset")->valuedouble;
        sensors.push_back(std::move(d));
    }
    cJSON_Delete(arr);
}
}  // namespace

Settings SettingsStore::load() const {
    nvs_handle_t h;
    if (nvs_open("chat", NVS_READONLY, &h) != ESP_OK) return {};
    Settings value;
    value.ssid = get(h, "ssid"); value.pass = get(h, "pass");
    const auto ntp = get(h, "ntp"); const auto timezone = get(h, "tz");
    if (!ntp.empty()) value.ntp_server = ntp;
    if (!timezone.empty()) value.timezone = timezone;
    uint8_t volume=3;if(nvs_get_u8(h,"volume",&volume)==ESP_OK)value.volume_level=volume<4?volume:3;
    uint8_t brightness=3;if(nvs_get_u8(h,"brightness",&brightness)==ESP_OK)value.brightness_level=brightness<4?brightness:3;
    auto load_service = [&](ServiceSettings& service, const char* prefix) {
        const std::string p(prefix);
        std::string provider;if(getExisting(h,(p+"_prov").c_str(),provider)&&!provider.empty())service.provider=provider;
        if(service.provider=="gemini"){
            service.url="https://generativelanguage.googleapis.com/v1beta";
            if(std::strcmp(prefix,"stt")==0){service.model="gemini-3.5-transcribe";service.voice.clear();}
            else if(std::strcmp(prefix,"llm")==0){service.model="gemini-3.5-flash-lite";service.voice.clear();}
            else{service.model="gemini-3.1-flash-tts-preview";service.voice="Leda";}
        }else if(service.provider=="anthropic"){
            service.url.clear();service.model.clear();service.voice.clear();
        }
        getExisting(h,(p+"_url").c_str(),service.url);getExisting(h,(p+"_model").c_str(),service.model);
        getExisting(h,(p+"_voice").c_str(),service.voice);getExisting(h,(p+"_key").c_str(),service.api_key);
        if(std::strcmp(prefix,"tts")==0){
            std::string instr;
            if(getExisting(h,(p+"_instr").c_str(),instr))service.instructions=instr;
            else service.instructions=DEFAULT_TTS_INSTRUCTIONS;
        }
    };
    load_service(value.stt,"stt"); load_service(value.llm,"llm"); load_service(value.tts,"tts");
    const auto stt_language=get(h,"stt_lang");if(!stt_language.empty())value.stt.language=stt_language;
    const auto agent=get(h,"llm_agent");if(!agent.empty())value.llm_agent=agent;
    value.llm_session_id=get(h,"llm_session");
    const auto mode=get(h,"conn_mode");if(mode=="integrated")value.connection_mode=mode;
    value.integrated.url=get(h,"int_url");value.integrated.api_key=get(h,"int_key");value.integrated.user=get(h,"int_user");value.integrated.session_key=get(h,"int_session");value.integrated.device_id=get(h,"int_device");value.integrated.voice=get(h,"int_voice");
    uint8_t correct=1;if(nvs_get_u8(h,"int_correct",&correct)==ESP_OK)value.integrated.correct_transcript=correct!=0;
    // Elasticsearch
    value.es.url=get(h,"es_url");value.es.user=get(h,"es_user");value.es.password=get(h,"es_pass");
    uint8_t es_en=0;if(nvs_get_u8(h,"es_en",&es_en)==ESP_OK)value.es.enabled=es_en!=0;
    // Sensor list (JSON blob)
    jsonToSensors(get(h,"sensors"), value.sensors);
    // IR
    uint8_t ir_en=0;if(nvs_get_u8(h,"ir_en",&ir_en)==ESP_OK)value.ir.enabled=ir_en!=0;
    uint8_t ir_tx=1;if(nvs_get_u8(h,"ir_tx",&ir_tx)==ESP_OK)value.ir.tx_mode=ir_tx==0?IrMode::INTERNAL:IrMode::EXTERNAL;
    uint8_t ir_rx=0;if(nvs_get_u8(h,"ir_rx",&ir_rx)==ESP_OK)value.ir.rx_mode=ir_rx==0?IrMode::INTERNAL:IrMode::EXTERNAL;
    uint16_t ir_port=0;if(nvs_get_u16(h,"ir_port",&ir_port)==ESP_OK&&ir_port>=1&&ir_port<=65535)value.ir.api_port=ir_port;
    value.ir.bearer_token=get(h,"ir_token");
    uint8_t speak_en=0;if(nvs_get_u8(h,"speak_en",&speak_en)==ESP_OK)value.speak_enabled=speak_en!=0;
    nvs_close(h); return value;
}

bool SettingsStore::save(const Settings& value) const {
    nvs_handle_t h;
    if (nvs_open("chat", NVS_READWRITE, &h) != ESP_OK) return false;
    auto put = [&](const char* key,const std::string& data){return nvs_set_str(h,key,data.c_str())==ESP_OK;};
    auto save_service = [&](const ServiceSettings& service,const char* prefix){
        const std::string p(prefix);bool ok=put((p+"_prov").c_str(),service.provider)&&put((p+"_url").c_str(),service.url)&&put((p+"_model").c_str(),service.model)&&put((p+"_voice").c_str(),service.voice)&&put((p+"_key").c_str(),service.api_key);
        if(std::strcmp(prefix,"tts")==0)ok=ok&&put((p+"_instr").c_str(),service.instructions);
        return ok;
    };
    bool ok = nvs_set_str(h, "ssid", value.ssid.c_str()) == ESP_OK &&
              nvs_set_str(h, "pass", value.pass.c_str()) == ESP_OK &&
              nvs_set_str(h, "ntp", value.ntp_server.c_str()) == ESP_OK &&
              nvs_set_str(h, "tz", value.timezone.c_str()) == ESP_OK &&
              nvs_set_u8(h, "volume", value.volume_level) == ESP_OK &&
              nvs_set_u8(h, "brightness", value.brightness_level) == ESP_OK &&
              nvs_set_str(h, "llm_agent", value.llm_agent.c_str()) == ESP_OK &&
              nvs_set_str(h, "llm_session", value.llm_session_id.c_str()) == ESP_OK &&
              nvs_set_str(h, "conn_mode", value.connection_mode.c_str()) == ESP_OK &&
              nvs_set_str(h, "int_url", value.integrated.url.c_str()) == ESP_OK &&
              nvs_set_str(h, "int_key", value.integrated.api_key.c_str()) == ESP_OK &&
              nvs_set_str(h, "int_user", value.integrated.user.c_str()) == ESP_OK &&
              nvs_set_str(h, "int_session", value.integrated.session_key.c_str()) == ESP_OK &&
              nvs_set_str(h, "int_device", value.integrated.device_id.c_str()) == ESP_OK &&
              nvs_set_str(h, "int_voice", value.integrated.voice.c_str()) == ESP_OK &&
              nvs_set_str(h, "stt_lang", value.stt.language.c_str()) == ESP_OK &&
              nvs_set_u8(h, "int_correct", value.integrated.correct_transcript?1:0) == ESP_OK &&
              save_service(value.stt,"stt") && save_service(value.llm,"llm") && save_service(value.tts,"tts") &&
              put("es_url", value.es.url) && put("es_user", value.es.user) && put("es_pass", value.es.password) && nvs_set_u8(h,"es_en",value.es.enabled?1:0)==ESP_OK &&
              put("sensors", sensorsToJson(value.sensors)) &&
              nvs_set_u8(h,"ir_en",value.ir.enabled?1:0)==ESP_OK &&
              nvs_set_u8(h,"ir_tx",value.ir.tx_mode==IrMode::INTERNAL?0:1)==ESP_OK &&
              nvs_set_u8(h,"ir_rx",value.ir.rx_mode==IrMode::INTERNAL?0:1)==ESP_OK &&
              nvs_set_u16(h,"ir_port",value.ir.api_port)==ESP_OK &&
              put("ir_token", value.ir.bearer_token) &&
              nvs_set_u8(h,"speak_en",value.speak_enabled?1:0)==ESP_OK &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h); return ok;
}
