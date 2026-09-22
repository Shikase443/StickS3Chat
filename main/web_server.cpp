#include "web_server.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "esp_random.h"

namespace {
const char LOGIN[] = R"(<!doctype html><html lang="en"><meta name="viewport" content="width=device-width"><style>body{font-family:sans-serif;max-width:28rem;margin:4rem auto;padding:1rem;background:#10141b;color:#eef}input,button{font-size:1.2rem;padding:.7rem;margin:.4rem 0;width:100%;box-sizing:border-box}button{background:#19b5a5;color:#fff;border:0}</style><h1>StickS3 WebUI</h1><form method="post" action="/login"><label>5-digit password</label><input name="password" type="password" inputmode="numeric" maxlength="5" required><button>Sign in</button></form></html>)";
std::string htmlEscape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '&') out += "&amp;"; else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;"; else if (c == '\"') out += "&quot;"; else out += c;
    }
    return out;
}
int hexValue(char c) { if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1; }
std::string decode(const std::string& value) {
    std::string out;
    for(size_t i=0;i<value.size();++i){if(value[i]=='+')out+=' ';else if(value[i]=='%'&&i+2<value.size()){int a=hexValue(value[i+1]),b=hexValue(value[i+2]);if(a>=0&&b>=0){out+=static_cast<char>((a<<4)|b);i+=2;}else out+=value[i];}else out+=value[i];}
    return out;
}
std::string field(const std::string& body, const char* name) {
    std::string key=std::string(name)+"="; size_t start=body.find(key);
    if(start==std::string::npos)return {};
    start+=key.size(); size_t end=body.find('&',start);
    return decode(body.substr(start,end==std::string::npos?std::string::npos:end-start));
}
std::string option(const char* value, const char* label, const std::string& selected) {
    return std::string("<option value=\"")+value+(selected==value?"\" selected>":"\">")+label+"</option>";
}
std::string providerOptions(const ServiceSettings& service, bool anthropic) {
    std::string out=option("openai","OpenAI (Compatible)",service.provider)+option("gemini","Gemini",service.provider);
    if(anthropic)out+=option("anthropic","Anthropic",service.provider);
    return out;
}
std::string serviceBlock(const char* id,const char* title,const ServiceSettings& service,bool voice,bool anthropic,bool agent_ui=false,const std::string& agent="none",const std::string& session="",bool language=false) {
    std::string p(id);
    std::string out="<fieldset><legend>"+std::string(title)+"</legend><label>Provider</label><select name=\""+p+"_provider\">"+providerOptions(service,anthropic)+"</select>";
    out+="<label>URL</label><input type=\"url\" name=\""+p+"_url\" value=\""+htmlEscape(service.url)+"\">";
    out+="<label>Model</label><input name=\""+p+"_model\" value=\""+htmlEscape(service.model)+"\">";
    if(language)out+="<label>Language</label><input name=\""+p+"_language\" list=\"languageOptions\" value=\""+htmlEscape(service.language)+"\"><datalist id=\"languageOptions\"><option value=\"Auto\"><option value=\"ja\"><option value=\"en\"><option value=\"ja-JP\"><option value=\"en-US\"></datalist>";
    if(voice)out+="<label>Voice</label><input name=\""+p+"_voice\" value=\""+htmlEscape(service.voice)+"\">";
    if(agent_ui){out+="<div id=\"agentFields\"><label>AI Agent</label><select id=\"llmAgent\" name=\"llm_agent\">"+option("none","None",agent)+option("openclaw","OpenClaw",agent)+option("hermes","Hermes Agent",agent)+"</select><div id=\"sessionField\"><label>Session ID</label><input name=\"llm_session\" value=\""+htmlEscape(session)+"\"></div></div>";}
    out+="<label>API Key</label><input type=\"password\" name=\""+p+"_key\" placeholder=\""+(service.api_key.empty()?"Not set":"Saved (leave blank to keep)")+"\"></fieldset>";
    return out;
}
}

bool WebServer::start(const Settings& config, SaveCallback callback, void* context) {
    if (server_) return true;
    settings_ = config;
    save_callback_ = callback; save_context_ = context;
    uint32_t p; do p = 10000 + esp_random() % 90000; while (p == previous_); previous_ = p;
    char value[6]; std::snprintf(value, sizeof(value), "%05lu", static_cast<unsigned long>(p)); password_ = value;
    char token[17]; std::snprintf(token, sizeof(token), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random()); token_ = token;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG(); cfg.stack_size = 12288;
    if (httpd_start(&server_, &cfg) != ESP_OK) { password_.clear(); token_.clear(); return false; }
    httpd_uri_t a{"/", HTTP_GET, root, this};
    httpd_uri_t b{"/login", HTTP_POST, login, this};
    httpd_uri_t c{"/settings", HTTP_GET, settings, this};
    httpd_uri_t d{"/save", HTTP_POST, save, this};
    httpd_register_uri_handler(server_, &a); httpd_register_uri_handler(server_, &b); httpd_register_uri_handler(server_, &c); httpd_register_uri_handler(server_, &d); return true;
}

void WebServer::stop() { if (server_) httpd_stop(server_); server_ = nullptr; password_.clear(); token_.clear(); }

esp_err_t WebServer::root(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (authenticated(r, self)) { httpd_resp_set_status(r,"303 See Other"); httpd_resp_set_hdr(r,"Location","/settings"); return httpd_resp_send(r,nullptr,0); }
    httpd_resp_set_type(r,"text/html; charset=utf-8"); return httpd_resp_send(r,LOGIN,HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::login(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx); char body[64]{};
    int n = httpd_req_recv(r, body, std::min<int>(r->content_len, sizeof(body)-1));
    if (n > 0 && std::string(body) == "password=" + self->password_) {
        std::string cookie = "stick_session=" + self->token_ + "; Path=/; HttpOnly; SameSite=Strict";
        httpd_resp_set_hdr(r,"Set-Cookie",cookie.c_str()); httpd_resp_set_status(r,"303 See Other"); httpd_resp_set_hdr(r,"Location","/settings"); return httpd_resp_send(r,nullptr,0);
    }
    httpd_resp_set_status(r,"401 Unauthorized"); return httpd_resp_sendstr(r,"Password incorrect");
}

esp_err_t WebServer::settings(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (!authenticated(r,self)) { httpd_resp_set_status(r,"303 See Other"); httpd_resp_set_hdr(r,"Location","/"); return httpd_resp_send(r,nullptr,0); }
    const auto& cfg=self->settings_;
    std::string zones=
        option("GMT+12","UTC-12:00",cfg.timezone)+option("GMT+11","UTC-11:00",cfg.timezone)+option("HST10","UTC-10:00",cfg.timezone)+
        option("GMT+9:30","UTC-09:30",cfg.timezone)+option("GMT+9","UTC-09:00",cfg.timezone)+option("GMT+8","UTC-08:00",cfg.timezone)+
        option("GMT+7","UTC-07:00",cfg.timezone)+option("GMT+6","UTC-06:00",cfg.timezone)+option("GMT+5","UTC-05:00",cfg.timezone)+
        option("GMT+4","UTC-04:00",cfg.timezone)+option("GMT+3:30","UTC-03:30",cfg.timezone)+option("GMT+3","UTC-03:00",cfg.timezone)+
        option("GMT+2","UTC-02:00",cfg.timezone)+option("GMT+1","UTC-01:00",cfg.timezone)+option("UTC0","UTC+00:00",cfg.timezone)+
        option("GMT-1","UTC+01:00",cfg.timezone)+option("GMT-2","UTC+02:00",cfg.timezone)+option("GMT-3","UTC+03:00",cfg.timezone)+
        option("GMT-3:30","UTC+03:30",cfg.timezone)+option("GMT-4","UTC+04:00",cfg.timezone)+option("GMT-4:30","UTC+04:30",cfg.timezone)+
        option("GMT-5","UTC+05:00",cfg.timezone)+option("GMT-5:30","UTC+05:30",cfg.timezone)+option("GMT-5:45","UTC+05:45",cfg.timezone)+
        option("GMT-6","UTC+06:00",cfg.timezone)+option("GMT-6:30","UTC+06:30",cfg.timezone)+option("GMT-7","UTC+07:00",cfg.timezone)+
        option("GMT-8","UTC+08:00",cfg.timezone)+option("GMT-8:45","UTC+08:45",cfg.timezone)+option("JST-9","UTC+09:00 — Japan",cfg.timezone)+
        option("GMT-9:30","UTC+09:30",cfg.timezone)+option("GMT-10","UTC+10:00",cfg.timezone)+option("GMT-10:30","UTC+10:30",cfg.timezone)+
        option("GMT-11","UTC+11:00",cfg.timezone)+option("GMT-12","UTC+12:00",cfg.timezone)+option("GMT-12:45","UTC+12:45",cfg.timezone)+
        option("GMT-13","UTC+13:00",cfg.timezone)+option("GMT-14","UTC+14:00",cfg.timezone);
    std::string page = "<!doctype html><html lang=\"en\"><meta name=\"viewport\" content=\"width=device-width\"><style>body{font-family:sans-serif;max-width:36rem;margin:2rem auto;padding:1rem;background:#10141b;color:#eef}fieldset{margin:1.2rem 0;padding:1rem;border:1px solid #445;border-radius:10px}legend{font-size:1.2rem;font-weight:bold}label{display:block;margin-top:.8rem}input,select,button{font-size:1rem;padding:.7rem;width:100%;box-sizing:border-box;background:#fff;color:#111;border:0;border-radius:4px}input[type=checkbox]{width:auto}.mode{display:flex;align-items:center;gap:.6rem}.mode input{margin:0}button{margin-top:1.5rem;background:#19b5a5;color:#fff}</style><h1>StickS3 Settings</h1><form method=\"post\" action=\"/save\"><fieldset><legend>Date &amp; Time</legend><label>NTP server</label><input name=\"ntp\" value=\"" + htmlEscape(cfg.ntp_server) + "\" required><label>Time zone</label><select name=\"timezone\">"+zones+"</select></fieldset><fieldset><legend>Connection Mode</legend><label class=\"mode\"><input id=\"modeSeparate\" type=\"checkbox\" name=\"connection_mode\" value=\"separate\""+(cfg.connection_mode=="separate"?" checked":"")+"> Separate APIs</label><label class=\"mode\"><input id=\"modeIntegrated\" type=\"checkbox\" name=\"connection_mode\" value=\"integrated\""+(cfg.connection_mode=="integrated"?" checked":"")+"> Integrated API</label></fieldset><div id=\"separateApis\">"+
        serviceBlock("stt","STT",cfg.stt,false,false,false,"none","",true)+serviceBlock("llm","LLM",cfg.llm,false,true,true,cfg.llm_agent,cfg.llm_session_id)+serviceBlock("tts","TTS",cfg.tts,true,false)+"</div><div id=\"integratedApi\"><fieldset><legend>Integrated API</legend><label>URL</label><input type=\"url\" name=\"int_url\" value=\""+htmlEscape(cfg.integrated.url)+"\"><label>API Key</label><input type=\"password\" name=\"int_key\" placeholder=\""+(cfg.integrated.api_key.empty()?"Not set":"Saved (leave blank to keep)")+"\"><label>User</label><input name=\"int_user\" value=\""+htmlEscape(cfg.integrated.user)+"\"><label>Session Key</label><input name=\"int_session\" value=\""+htmlEscape(cfg.integrated.session_key)+"\"><label>Device ID</label><input name=\"int_device\" value=\""+htmlEscape(cfg.integrated.device_id)+"\"><label>Voice</label><input name=\"int_voice\" value=\""+htmlEscape(cfg.integrated.voice)+"\"><label><input type=\"checkbox\" name=\"int_correct\" value=\"1\""+(cfg.integrated.correct_transcript?" checked":"")+"> Correct Transcript</label></fieldset></div>"+
        "<button>Save</button></form><script>const ms=document.getElementById('modeSeparate'),mi=document.getElementById('modeIntegrated'),sep=document.getElementById('separateApis'),integ=document.getElementById('integratedApi'),p=document.querySelector('[name=llm_provider]'),a=document.getElementById('llmAgent'),af=document.getElementById('agentFields'),sf=document.getElementById('sessionField');const defaults={stt:{openai:{url:'https://api.openai.com/v1/audio/transcriptions',model:'gpt-4o-transcribe'},gemini:{url:'https://generativelanguage.googleapis.com/v1beta',model:'gemini-3.5-transcribe'}},llm:{openai:{url:'https://api.openai.com/v1/responses',model:'gpt-5.6-luna'},gemini:{url:'https://generativelanguage.googleapis.com/v1beta',model:'gemini-3.5-flash-lite'},anthropic:{url:'',model:''}},tts:{openai:{url:'https://api.openai.com/v1/audio/speech',model:'gpt-4o-mini-tts',voice:'marin'},gemini:{url:'https://generativelanguage.googleapis.com/v1beta',model:'gemini-3.1-flash-tts-preview',voice:''}}};function bind(id){const provider=document.querySelector(`[name=${id}_provider]`),url=document.querySelector(`[name=${id}_url]`),model=document.querySelector(`[name=${id}_model]`),voice=document.querySelector(`[name=${id}_voice]`),cache={};let current=provider.value;function read(){return{url:url.value,model:model.value,voice:voice?voice.value:''}}function write(v){url.value=v.url||'';model.value=v.model||'';if(voice)voice.value=v.voice||''}cache[current]=read();provider.addEventListener('change',()=>{cache[current]=read();current=provider.value;write(cache[current]||defaults[id][current]||{url:'',model:'',voice:''})})}bind('stt');bind('llm');bind('tts');function toggle(){sep.style.display=ms.checked?'block':'none';integ.style.display=mi.checked?'block':'none';af.style.display=p.value==='openai'?'block':'none';sf.style.display=p.value==='openai'&&a.value!=='none'?'block':'none'}function choose(e){if(e.target===ms){ms.checked=true;mi.checked=false}else{mi.checked=true;ms.checked=false}toggle()}ms.addEventListener('change',choose);mi.addEventListener('change',choose);p.addEventListener('change',toggle);a.addEventListener('change',toggle);toggle()</script></html>";
    httpd_resp_set_type(r,"text/html; charset=utf-8"); return httpd_resp_send(r,page.c_str(),page.size());
}

esp_err_t WebServer::save(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (!authenticated(r,self)) { httpd_resp_set_status(r,"303 See Other"); httpd_resp_set_hdr(r,"Location","/"); return httpd_resp_send(r,nullptr,0); }
    if (r->content_len <= 0 || r->content_len > 4096) { httpd_resp_set_status(r,"400 Bad Request"); return httpd_resp_sendstr(r,"Invalid settings"); }
    std::string body(r->content_len, '\0'); int received=0;
    while(received<r->content_len){int n=httpd_req_recv(r,body.data()+received,r->content_len-received);if(n<=0)return ESP_FAIL;received+=n;}
    Settings updated=self->settings_;
    updated.ntp_server=field(body,"ntp"); updated.timezone=field(body,"timezone");
    auto update_service=[&](ServiceSettings& service,const char* id,bool allow_anthropic,bool voice){
        std::string p(id);const auto provider=field(body,(p+"_provider").c_str());
        service.provider=(provider=="gemini"||(allow_anthropic&&provider=="anthropic"))?provider:"openai";
        service.url=field(body,(p+"_url").c_str());service.model=field(body,(p+"_model").c_str());
        if(voice)service.voice=field(body,(p+"_voice").c_str());
        const auto key=field(body,(p+"_key").c_str());if(!key.empty())service.api_key=key;
    };
    update_service(updated.stt,"stt",false,false);update_service(updated.llm,"llm",true,false);update_service(updated.tts,"tts",false,true);
    updated.stt.language=field(body,"stt_language");if(updated.stt.language.empty())updated.stt.language="Auto";
    const auto agent=field(body,"llm_agent");if(agent=="none"||agent=="openclaw"||agent=="hermes")updated.llm_agent=agent;
    const auto session=field(body,"llm_session");if(!session.empty()||body.find("llm_session=")!=std::string::npos)updated.llm_session_id=session;
    const auto mode=field(body,"connection_mode");updated.connection_mode=mode=="integrated"?"integrated":"separate";
    updated.integrated.url=field(body,"int_url");updated.integrated.user=field(body,"int_user");updated.integrated.session_key=field(body,"int_session");updated.integrated.device_id=field(body,"int_device");updated.integrated.voice=field(body,"int_voice");updated.integrated.correct_transcript=body.find("int_correct=1")!=std::string::npos;
    const auto integrated_key=field(body,"int_key");if(!integrated_key.empty())updated.integrated.api_key=integrated_key;
    if(updated.ntp_server.empty()||updated.timezone.empty()){httpd_resp_set_status(r,"400 Bad Request");return httpd_resp_sendstr(r,"Invalid settings");}
    self->settings_=updated;
    if(self->save_callback_)self->save_callback_(self->save_context_,updated);
    httpd_resp_set_status(r,"303 See Other");httpd_resp_set_hdr(r,"Location","/settings");return httpd_resp_send(r,nullptr,0);
}

bool WebServer::authenticated(httpd_req_t* r, const WebServer* self) {
    size_t n = httpd_req_get_hdr_value_len(r,"Cookie"); if (!n || n > 255) return false;
    char cookie[256]; if (httpd_req_get_hdr_value_str(r,"Cookie",cookie,sizeof(cookie)) != ESP_OK) return false;
    return std::strstr(cookie,("stick_session="+self->token_).c_str()) != nullptr;
}
