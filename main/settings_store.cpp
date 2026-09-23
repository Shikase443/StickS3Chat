#include "settings_store.hpp"
#include <cstring>
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
              nvs_commit(h) == ESP_OK;
    nvs_close(h); return ok;
}
