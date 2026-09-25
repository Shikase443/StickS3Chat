#include "web_server.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "esp_random.h"
#include "cJSON.h"
#include "M5Unified.h"

namespace {
const char LOGIN[] = R"(<!doctype html><html lang="en"><meta name="viewport" content="width=device-width"><style>body{font-family:sans-serif;max-width:28rem;margin:4rem auto;padding:1rem;background:#10141b;color:#eef}input,button{font-size:1.2rem;padding:.7rem;margin:.4rem 0;width:100%;box-sizing:border-box}button{background:#19b5a5;color:#fff;border:0}</style><h1>StickS3 WebUI</h1><form method="post" action="/login"><label>5-digit password</label><input name="password" type="password" inputmode="numeric" maxlength="5" required><button>Sign in</button></form></html>)";
const char FACE_UPLOAD_HTML[] = R"html(<style>.faceRow{display:flex;align-items:center;gap:.6rem;margin-top:.6rem}.faceRow label{flex:0 0 9rem;margin:0}.faceRow input{flex:1}.faceStatus{flex:0 0 6.5rem;font-size:.85rem;color:#9ab;text-align:right}.faceMode{display:flex;gap:1.5rem;margin:.6rem 0}.faceMode label{display:inline;flex:0;margin:0;font-size:1rem}.faceMode input{width:auto;margin-right:.3rem}</style><fieldset style="margin-top:1.5rem"><legend>Face</legend><div class="faceMode"><label><input type="radio" name="face_mode" value="image" checked> Image face</label><label><input type="radio" name="face_mode" value="vector"> Vector face</label></div><div id="faceUploadArea"><p style="margin:.4rem 0;color:#9ab;font-size:.9rem">Upload PNG/JPG. Each is resized to 114x114, alpha composited on black, and stored as RGB565.</p><div class="faceRow"><label>Normal</label><input type="file" accept="image/*" data-face="normal"><span class="faceStatus" data-status="normal"></span></div><div class="faceRow"><label>Smile</label><input type="file" accept="image/*" data-face="smile"><span class="faceStatus" data-status="smile"></span></div><div class="faceRow"><label>Surprised (recording)</label><input type="file" accept="image/*" data-face="surprised"><span class="faceStatus" data-status="surprised"></span></div><div class="faceRow"><label>Mouth (medium)</label><input type="file" accept="image/*" data-face="mouth_medium"><span class="faceStatus" data-status="mouth_medium"></span></div><div class="faceRow"><label>Mouth (large)</label><input type="file" accept="image/*" data-face="mouth_large"><span class="faceStatus" data-status="mouth_large"></span></div></div></fieldset>)html";
const char FACE_UPLOAD_JS[] = R"js((function(){function uploadFace(file,faceId,statusEl){statusEl.textContent='Processing...';const img=new Image();const url=URL.createObjectURL(file);img.onload=function(){URL.revokeObjectURL(url);const size=114;const canvas=document.createElement('canvas');canvas.width=size;canvas.height=size;const ctx=canvas.getContext('2d',{willReadFrequently:true});ctx.fillStyle='#000';ctx.fillRect(0,0,size,size);const scale=Math.max(size/img.width,size/img.height);const w=img.width*scale,h=img.height*scale;ctx.drawImage(img,(size-w)/2,(size-h)/2,w,h);const data=ctx.getImageData(0,0,size,size).data;const out=new Uint8Array(size*size*2);let p=0;for(let i=0;i<size*size;i++){const r=data[i*4],g=data[i*4+1],b=data[i*4+2];const v=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);out[p++]=(v>>8)&0xff;out[p++]=v&0xff;}statusEl.textContent='Uploading...';fetch('/upload?face='+faceId,{method:'POST',body:out.buffer}).then(function(res){statusEl.textContent=res.ok?'Saved':'Error '+res.status;}).catch(function(){statusEl.textContent='Upload failed';});};img.onerror=function(){URL.revokeObjectURL(url);statusEl.textContent='Load failed';};img.src=url;}document.querySelectorAll('input[type=file][data-face]').forEach(function(input){input.addEventListener('change',function(){if(input.files&&input.files[0]){const statusEl=document.querySelector('[data-status="'+input.dataset.face+'"]');uploadFace(input.files[0],input.dataset.face,statusEl);}});});var faceModeRadios=document.querySelectorAll('input[name=face_mode]');var faceUploadArea=document.getElementById('faceUploadArea');function toggleFaceUpload(){faceUploadArea.style.display=(document.querySelector('input[name=face_mode]:checked').value==='image')?'block':'none';}faceModeRadios.forEach(function(r){r.addEventListener('change',toggleFaceUpload);});toggleFaceUpload();})();)js";
const char BLE_SENSORS_HTML[] = R"ble(<style>.sensorCard{border:1px solid #445;border-radius:8px;padding:.8rem;margin:.8rem 0;background:#161b24}.sensorHead{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap}.sensorHead input[name=s_name]{flex:1;min-width:8rem}.sensorHead select{width:auto;max-width:14rem}.sensorHead label{margin:0;font-size:.9rem}.delSensor{background:#a33;color:#fff;border:0;padding:.5rem .8rem;border-radius:4px;cursor:pointer}.sensorCard label{font-size:.85rem;color:#9ab}.chan{border-left:2px solid #445;padding-left:.6rem;margin:.6rem 0}.chan .chanTitle{font-weight:bold;color:#cde;font-size:.9rem;margin-bottom:.3rem}.chan .row{display:flex;gap:.5rem;align-items:center;margin:.35rem 0}.chan .row label{flex:0 0 4.5rem;margin:0}.chan .row input{flex:1}.s400Fields{display:none;margin-top:.5rem;padding-top:.5rem;border-top:1px dashed #445}#addSensor{margin-top:1rem;background:#2a6;color:#fff;border:0;padding:.7rem;width:100%;border-radius:4px;cursor:pointer}</style><fieldset><legend>BLE Sensors / Elasticsearch</legend><label><input type="checkbox" name="es_enabled" value="1" __ES_ENABLED__> Enable BLE Sensors / Elasticsearch</label><label>ES Base URL</label><input type="url" name="es_url" value="__ES_URL__" placeholder="http://es:9200"><label>ES User</label><input name="es_user" value="__ES_USER__"><label>ES Password</label><input type="password" name="es_pass" placeholder="__ES_PASS_PH__"><div id="sensorList"></div><button type="button" id="addSensor">Add device</button><input type="hidden" name="sensors_json" id="sensorsJson" value="__SENSORS_JSON__"></fieldset>)ble";
const char API_SERVER_HTML[] = R"api(<fieldset style="margin-top:1.5rem"><legend>API Server</legend><p style="margin:.2rem 0;color:#9ab;font-size:.85rem">All APIs are on port 80, always available while Wi-Fi is connected.</p><label>Bearer Token</label><input type="password" name="ir_token" placeholder="__IR_TOKEN_PH__"><hr style="border:0;border-top:1px solid #445;margin:.8rem 0"><label><input type="checkbox" name="ir_enabled" value="1" __IR_ENABLED__> Enable IR API</label><label>Transmit</label><select name="ir_tx"><option value="external" __IR_TX_EXT__>External (Grove, GPIO9)</option><option value="internal" __IR_TX_INT__>Internal (GPIO46)</option></select><label>Receive</label><select name="ir_rx"><option value="internal" __IR_RX_INT__>Internal (GPIO42)</option><option value="external" __IR_RX_EXT__>External (Grove, GPIO10)</option></select><hr style="border:0;border-top:1px solid #445;margin:.8rem 0"><label><input type="checkbox" name="speak_enabled" value="1" __SPEAK_ENABLED__> Enable Speech API</label></fieldset>)api";
const char BLE_SENSORS_JS[] = R"js((function(){
var TYPE_LABEL={illuminance:'Sizuku LUX',env:'SwitchBot (SW3400010)',enocean:'EnOcean (STM 550B)',xiaomi_s400:'Xiaomi S400'};
var CHANNELS={illuminance:['Illuminance'],env:['Humidity','Temperature'],enocean:['Humidity','Temperature','Illuminance'],xiaomi_s400:[]};
var list=document.getElementById('sensorList');
var jsonEl=document.getElementById('sensorsJson');
var addBtn=document.getElementById('addSensor');
function esc(v){return (v==null?'':String(v)).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
function typeOptions(type){var h='';['illuminance','env','enocean','xiaomi_s400'].forEach(function(t){h+='<option value="'+t+'"'+(t===type?' selected':'')+'>'+TYPE_LABEL[t]+'</option>';});return h;}
function chanHtml(label,ch){ch=ch||{index:'',field:''};return '<div class="chan"><div class="chanTitle">'+label+'</div><div class="row"><label>index</label><input name="c_index" value="'+esc(ch.index)+'"></div><div class="row"><label>field</label><input name="c_field" value="'+esc(ch.field)+'"></div></div>';}
function cardHtml(d){
  var type=d.type||'illuminance';
  var labels=CHANNELS[type]||[];
  var chans=d.channels||[];
  var h='<div class="sensorCard" data-type="'+esc(type)+'">';
  h+='<div class="sensorHead"><input name="s_name" placeholder="Name" value="'+esc(d.name)+'">';
  h+='<select name="s_type">'+typeOptions(type)+'</select>';
  h+='<button type="button" class="delSensor">Delete</button></div>';
  h+='<label>MAC</label><input name="s_mac" placeholder="aa:bb:cc:dd:ee:ff" value="'+esc(d.mac)+'">';
  h+='<div class="s_rateWrap"><label>Rate (ms)</label><input name="s_rate" type="number" value="'+(d.rate_ms||30000)+'"></div>';
  h+='<div class="s_chans">';
  for(var i=0;i<labels.length;i++){h+=chanHtml(labels[i],chans[i]);}
  h+='</div>';
  h+='<div class="s400Fields"><label>index</label><input name="s_index" value="'+esc(d.index||'')+'">';
  h+='<label>Bindkey (32 hex)</label><input name="s_bindkey" maxlength="32" value="'+esc(d.bindkey||'')+'">';
  h+='<label>Height (cm)</label><input name="s_height" type="number" step="0.1" value="'+(d.height_cm||'')+'">';
  h+='<label>Birth date</label><input name="s_birth" placeholder="YYYY-MM-DD" value="'+esc(d.birth_date||'')+'">';
  h+='<label>Fat offset</label><input name="s_fatoff" type="number" step="any" value="'+(d.fat_offset||0)+'"></div>';
  h+='</div>';
  return h;
}
function syncCard(card){
  var type=card.querySelector('[name=s_type]').value;
  var labels=CHANNELS[type]||[];
  var wrap=card.querySelector('.s_chans');
  var old=wrap.querySelectorAll('.chan');
  var oldVals=[];old.forEach(function(c){oldVals.push({index:c.querySelector('[name=c_index]').value,field:c.querySelector('[name=c_field]').value});});
  wrap.innerHTML='';
  for(var i=0;i<labels.length;i++){var tmp=document.createElement('div');tmp.innerHTML=chanHtml(labels[i],oldVals[i]);wrap.appendChild(tmp.firstChild);}
  card.querySelector('.s400Fields').style.display=(type=='xiaomi_s400')?'block':'none';
  card.querySelector('.s_rateWrap').style.display=(type=='xiaomi_s400')?'none':'block';
  card.dataset.type=type;
}
function collect(){
  var out=[];
  list.querySelectorAll('.sensorCard').forEach(function(card){
    var type=card.querySelector('[name=s_type]').value;
    var d={name:card.querySelector('[name=s_name]').value,mac:card.querySelector('[name=s_mac]').value,type:type,
      rate_ms:parseInt(card.querySelector('[name=s_rate]').value)||30000,channels:[]};
    card.querySelectorAll('.chan').forEach(function(c){d.channels.push({index:c.querySelector('[name=c_index]').value,field:c.querySelector('[name=c_field]').value});});
    if(type=='xiaomi_s400'){d.index=card.querySelector('[name=s_index]').value;d.bindkey=card.querySelector('[name=s_bindkey]').value;d.height_cm=parseFloat(card.querySelector('[name=s_height]').value)||0;d.birth_date=card.querySelector('[name=s_birth]').value;d.fat_offset=parseFloat(card.querySelector('[name=s_fatoff]').value)||0;}
    out.push(d);
  });
  return out;
}
function render(devices){list.innerHTML='';devices.forEach(function(d){var tmp=document.createElement('div');tmp.innerHTML=cardHtml(d);var card=tmp.firstChild;list.appendChild(card);bindCard(card);});}
function bindCard(card){card.querySelector('[name=s_type]').addEventListener('change',function(){syncCard(card);});card.querySelector('.delSensor').addEventListener('click',function(){card.remove();});syncCard(card);}
addBtn.addEventListener('click',function(){if(list.querySelectorAll('.sensorCard').length>=5){alert('Max 5 devices');return;}var tmp=document.createElement('div');tmp.innerHTML=cardHtml({name:'',mac:'',type:'illuminance',rate_ms:30000,channels:[],index:'',bindkey:'',height_cm:0,birth_date:'',fat_offset:0});var card=tmp.firstChild;list.appendChild(card);bindCard(card);});
var form=document.querySelector('form');
form.addEventListener('submit',function(){jsonEl.value=JSON.stringify(collect());});
var initial=[];try{initial=JSON.parse(jsonEl.value||'[]');}catch(e){initial=[];}
render(initial);
})();)js";
std::string sensorsToJson(const Settings& cfg) {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& d : cfg.sensors) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", d.name.c_str());
        char mac[18]; std::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", d.mac[0],d.mac[1],d.mac[2],d.mac[3],d.mac[4],d.mac[5]);
        cJSON_AddStringToObject(o, "mac", mac);
        const char* tn = d.type==SensorType::ILLUMINANCE?"illuminance":d.type==SensorType::ENV?"env":d.type==SensorType::ENOCEAN?"enocean":"xiaomi_s400";
        cJSON_AddStringToObject(o, "type", tn);
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
        if(!d.bindkey.empty()) cJSON_AddStringToObject(o, "bindkey", d.bindkey.c_str());
        if(d.height_cm!=0.0f) cJSON_AddNumberToObject(o, "height_cm", (double)d.height_cm);
        if(!d.birth_date.empty()) cJSON_AddStringToObject(o, "birth_date", d.birth_date.c_str());
        if(d.body_fat_offset!=0.0f) cJSON_AddNumberToObject(o, "fat_offset", (double)d.body_fat_offset);
        cJSON_AddItemToArray(arr, o);
    }
    char* out = cJSON_PrintUnformatted(arr);
    std::string r = out ? out : "[]";
    if (out) cJSON_free(out);
    cJSON_Delete(arr);
    return r;
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
        if (cJSON_IsString(cJSON_GetObjectItem(o, "mac"))) {
            unsigned v[6];
            if (std::sscanf(cJSON_GetObjectItem(o, "mac")->valuestring, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0],&v[1],&v[2],&v[3],&v[4],&v[5]) == 6)
                for (int i=0;i<6;i++) d.mac[i]=(uint8_t)v[i];
        }
        if (cJSON_IsString(cJSON_GetObjectItem(o, "type"))) {
            const char* t = cJSON_GetObjectItem(o, "type")->valuestring;
            if (!strcmp(t,"env")) d.type=SensorType::ENV;
            else if (!strcmp(t,"enocean")) d.type=SensorType::ENOCEAN;
            else if (!strcmp(t,"xiaomi_s400")) d.type=SensorType::XIAOMI_S400;
            else d.type=SensorType::ILLUMINANCE;
        }
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
std::string serviceBlock(const char* id,const char* title,const ServiceSettings& service,bool voice,bool anthropic,bool agent_ui=false,const std::string& agent="none",const std::string& session="",bool language=false,bool instructions=false) {
    std::string p(id);
    std::string out="<fieldset><legend>"+std::string(title)+"</legend><label>Provider</label><select name=\""+p+"_provider\">"+providerOptions(service,anthropic)+"</select>";
    out+="<label>URL</label><input type=\"url\" name=\""+p+"_url\" value=\""+htmlEscape(service.url)+"\">";
    out+="<label>Model</label><input name=\""+p+"_model\" value=\""+htmlEscape(service.model)+"\">";
    if(language)out+="<label>Language</label><input name=\""+p+"_language\" list=\"languageOptions\" value=\""+htmlEscape(service.language)+"\"><datalist id=\"languageOptions\"><option value=\"Auto\"><option value=\"ja\"><option value=\"en\"><option value=\"ja-JP\"><option value=\"en-US\"></datalist>";
    if(voice)out+="<label>Voice</label><input name=\""+p+"_voice\" value=\""+htmlEscape(service.voice)+"\">";
    if(instructions)out+="<label>TTS Instructions</label><textarea name=\""+p+"_instr\" rows=\"4\" style=\"width:100%;box-sizing:border-box;"+(voice?"":"")+"\">"+htmlEscape(service.instructions)+"</textarea><p style=\"margin:.4rem 0 0;color:#e55;font-size:.85rem\">It allows for the control of emotional range, intonation, impression, speaking rate, tone, and whispering, and is optimized for English.</p>";
    if(agent_ui){out+="<div id=\"agentFields\"><label>AI Agent</label><select id=\"llmAgent\" name=\"llm_agent\">"+option("none","None",agent)+option("openclaw","OpenClaw",agent)+option("hermes","Hermes Agent",agent)+"</select><div id=\"sessionField\"><label>Session ID</label><input name=\"llm_session\" value=\""+htmlEscape(session)+"\"></div></div>";}
    out+="<label>API Key</label><input type=\"password\" name=\""+p+"_key\" placeholder=\""+(service.api_key.empty()?"Not set":"Saved (leave blank to keep)")+"\"></fieldset>";
    return out;
}
bool parseFaceId(const std::string& id, FaceExpression& out) {
    if (id == "normal") { out = FaceExpression::NORMAL; return true; }
    if (id == "smile") { out = FaceExpression::SMILE; return true; }
    if (id == "surprised") { out = FaceExpression::SURPRISED; return true; }
    if (id == "mouth_medium") { out = FaceExpression::MOUTH_MEDIUM; return true; }
    if (id == "mouth_large") { out = FaceExpression::MOUTH_LARGE; return true; }
    return false;
}
}

// ---- IR API (ポート80上で常時提供) ----
namespace {
esp_err_t irJsonReply(httpd_req_t* r, int status, const char* code, const char* message) {
    const char* st = status==202?"202 Accepted":status==400?"400 Bad Request":
        status==401?"401 Unauthorized":status==408?"408 Request Timeout":
        status==409?"409 Conflict":status==429?"429 Too Many Requests":"500 Internal Server Error";
    httpd_resp_set_status(r, st);
    httpd_resp_set_type(r, "application/json");
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "code", code);
    cJSON_AddStringToObject(o, "message", message);
    char* body = cJSON_PrintUnformatted(o);
    esp_err_t e = httpd_resp_sendstr(r, body);
    free(body); cJSON_Delete(o);
    return e;
}
bool irReadBody(httpd_req_t* r, size_t max_len, std::string& out) {
    if (r->content_len <= 0 || r->content_len > max_len) return false;
    out.resize(r->content_len);
    int got = 0;
    while (got < r->content_len) {
        int n = httpd_req_recv(r, &out[got], r->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return false;
        got += n;
    }
    return true;
}
}  // namespace

bool WebServer::irAuthorized(httpd_req_t* r, const WebServer* self) {
    if (!self->ir_api_) return false;
    const IrSettings& ir = self->ir_api_->settings();
    if (!ir.enabled) return false;
    if (ir.bearer_token.empty()) return true;  // トークン未設定なら認証なし
    size_t n = httpd_req_get_hdr_value_len(r, "Authorization");
    if (!n || n > 511) return false;
    char h[512];
    if (httpd_req_get_hdr_value_str(r, "Authorization", h, sizeof(h)) != ESP_OK) return false;
    char expected[512];
    std::snprintf(expected, sizeof(expected), "Bearer %s", ir.bearer_token.c_str());
    return std::strlen(h) == std::strlen(expected) && std::memcmp(h, expected, std::strlen(expected)) == 0;
}

esp_err_t WebServer::irSend(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (!irAuthorized(r, self)) { httpd_resp_set_hdr(r, "WWW-Authenticate", "Bearer"); return irJsonReply(r, 401, "unauthorized", "invalid bearer token"); }
    std::string body;
    if (!irReadBody(r, 16384, body)) return irJsonReply(r, 400, "invalid_request", "JSON body must be 1..16384 bytes");
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return irJsonReply(r, 400, "invalid_json", "malformed JSON");
    IrTxRequest tx;
    cJSON* carrier = cJSON_GetObjectItemCaseSensitive(root, "carrier_hz");
    if (carrier) {
        if (!cJSON_IsNumber(carrier) || carrier->valuedouble < 20000 || carrier->valuedouble > 60000) { cJSON_Delete(root); return irJsonReply(r, 400, "invalid_carrier", "carrier_hz must be 20000..60000"); }
        tx.carrier_hz = (uint32_t)carrier->valuedouble;
    }
    cJSON* repeat = cJSON_GetObjectItemCaseSensitive(root, "repeat");
    if (repeat) {
        if (!cJSON_IsNumber(repeat) || repeat->valuedouble < 1 || repeat->valuedouble > 10) { cJSON_Delete(root); return irJsonReply(r, 400, "invalid_repeat", "repeat must be 1..10"); }
        tx.repeat = (uint8_t)repeat->valuedouble;
    }
    cJSON* durations = cJSON_GetObjectItemCaseSensitive(root, "durations_us");
    if (!cJSON_IsArray(durations)) { cJSON_Delete(root); return irJsonReply(r, 400, "invalid_durations", "durations_us must be an array"); }
    int count = cJSON_GetArraySize(durations);
    if (count < 2 || count > kIrMaxDurations) { cJSON_Delete(root); return irJsonReply(r, 400, "invalid_durations", "durations_us must contain 2..1024 values"); }
    cJSON* v = nullptr; int idx = 0;
    cJSON_ArrayForEach(v, durations) {
        if (!cJSON_IsNumber(v) || v->valuedouble < 1 || v->valuedouble > 65535) { cJSON_Delete(root); return irJsonReply(r, 400, "invalid_duration", "each duration must be 1..65535 us"); }
        tx.durations_us[idx++] = (uint16_t)v->valuedouble;
    }
    tx.duration_count = (uint16_t)count;
    cJSON_Delete(root);
    if (!self->ir_api_->engine().enqueue(tx)) return irJsonReply(r, 429, "queue_full", "IR queue is full");
    return irJsonReply(r, 202, "accepted", "IR signal queued");
}

esp_err_t WebServer::irLearn(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (!irAuthorized(r, self)) { httpd_resp_set_hdr(r, "WWW-Authenticate", "Bearer"); return irJsonReply(r, 401, "unauthorized", "invalid bearer token"); }
    std::string body;
    if (!irReadBody(r, 1024, body)) return irJsonReply(r, 400, "invalid_request", "JSON body must be 1..1024 bytes");
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return irJsonReply(r, 400, "invalid_json", "malformed JSON");
    cJSON* timeout = cJSON_GetObjectItemCaseSensitive(root, "timeout_ms");
    if (!cJSON_IsNumber(timeout) || timeout->valuedouble < 100 || timeout->valuedouble > 30000) { cJSON_Delete(root); return irJsonReply(r, 400, "invalid_timeout", "timeout_ms must be 100..30000"); }
    uint32_t timeout_ms = (uint32_t)timeout->valuedouble;
    cJSON_Delete(root);

    auto& ir = *self->ir_api_;
    bool expected = false;
    if (!ir.learningActive().compare_exchange_strong(expected, true)) {
        return irJsonReply(r, 409, "learn_busy", "IR learning is already active");
    }
    M5.Speaker.end();
    vTaskDelay(pdMS_TO_TICKS(200));
    IrLearnResult result;
    bool ok = ir.engine().learn(timeout_ms, result);
    M5.Speaker.begin();
    ir.learningActive().store(false);

    if (!ok) return irJsonReply(r, 408, "learn_timeout", "No IR signal received");
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "carrier_hz", 38000);
    cJSON_AddNumberToObject(resp, "repeat", 1);
    cJSON* arr = cJSON_AddArrayToObject(resp, "durations_us");
    for (uint16_t i = 0; i < result.duration_count; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(result.durations_us[i]));
    char* json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!json) return irJsonReply(r, 500, "internal_error", "out of memory");
    httpd_resp_set_type(r, "application/json");
    esp_err_t e = httpd_resp_sendstr(r, json);
    free(json);
    return e;
}

esp_err_t WebServer::apiSpeak(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    // 無効時はIR APIと同様に401。
    if (!self->settings_.speak_enabled) { httpd_resp_set_hdr(r, "WWW-Authenticate", "Bearer"); return irJsonReply(r, 401, "disabled", "Speech API is disabled"); }
    // BearerトークンはIR APIと共有。
    if (!irAuthorized(r, self)) { httpd_resp_set_hdr(r, "WWW-Authenticate", "Bearer"); return irJsonReply(r, 401, "unauthorized", "invalid bearer token"); }
    std::string body;
    if (!irReadBody(r, 4096, body)) return irJsonReply(r, 400, "invalid_request", "JSON body must be 1..4096 bytes");
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return irJsonReply(r, 400, "invalid_json", "malformed JSON");
    cJSON* text = cJSON_GetObjectItemCaseSensitive(root, "text");
    if (!cJSON_IsString(text) || !text->valuestring[0] || std::strlen(text->valuestring) > 1024) { cJSON_Delete(root); return irJsonReply(r, 400, "invalid_text", "text must be 1..1024 bytes"); }
    std::string speak_text(text->valuestring);
    cJSON_Delete(root);
    if (!self->voice_) return irJsonReply(r, 503, "voice_unavailable", "Voice is not ready");
    if (!self->voice_->speak(speak_text, self->settings_)) return irJsonReply(r, 429, "queue_full", "Speech queue is full");
    return irJsonReply(r, 202, "accepted", "Speech queued");
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
    httpd_uri_t e{"/upload", HTTP_POST, upload, this};
    httpd_uri_t f{"/ir/send", HTTP_POST, irSend, this};
    httpd_uri_t g{"/ir/learn", HTTP_POST, irLearn, this};
    httpd_uri_t h{"/api/speak", HTTP_POST, apiSpeak, this};
    httpd_register_uri_handler(server_, &a); httpd_register_uri_handler(server_, &b); httpd_register_uri_handler(server_, &c); httpd_register_uri_handler(server_, &d); httpd_register_uri_handler(server_, &e); httpd_register_uri_handler(server_, &f); httpd_register_uri_handler(server_, &g); httpd_register_uri_handler(server_, &h); return true;
}

void WebServer::stop() { if (server_) httpd_stop(server_); server_ = nullptr; password_.clear(); token_.clear(); }

esp_err_t WebServer::root(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (settingsAccessDenied(r, self)) return ESP_OK;
    if (authenticated(r, self)) { httpd_resp_set_status(r,"303 See Other"); httpd_resp_set_hdr(r,"Location","/settings"); return httpd_resp_send(r,nullptr,0); }
    httpd_resp_set_type(r,"text/html; charset=utf-8"); return httpd_resp_send(r,LOGIN,HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::login(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (settingsAccessDenied(r, self)) return ESP_OK;
    char body[64]{};
    int n = httpd_req_recv(r, body, std::min<int>(r->content_len, sizeof(body)-1));
    if (n > 0 && std::string(body) == "password=" + self->password_) {
        std::string cookie = "stick_session=" + self->token_ + "; Path=/; HttpOnly; SameSite=Strict";
        httpd_resp_set_hdr(r,"Set-Cookie",cookie.c_str()); httpd_resp_set_status(r,"303 See Other"); httpd_resp_set_hdr(r,"Location","/settings"); return httpd_resp_send(r,nullptr,0);
    }
    httpd_resp_set_status(r,"401 Unauthorized"); return httpd_resp_sendstr(r,"Password incorrect");
}

esp_err_t WebServer::settings(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (settingsAccessDenied(r, self)) return ESP_OK;
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
    std::string bleSection = BLE_SENSORS_HTML;
    auto sub = [&](const char* ph, const std::string& v){ size_t p=bleSection.find(ph); if(p!=std::string::npos) bleSection.replace(p, std::strlen(ph), v); };
    sub("__ES_ENABLED__", cfg.es.enabled ? " checked" : "");
    sub("__ES_URL__", htmlEscape(cfg.es.url));
    sub("__ES_USER__", htmlEscape(cfg.es.user));
    sub("__ES_PASS_PH__", cfg.es.password.empty() ? "Not set" : "Saved (leave blank to keep)");
    sub("__SENSORS_JSON__", htmlEscape(sensorsToJson(cfg)));
    std::string apiSection = API_SERVER_HTML;
    auto apisub = [&](const char* ph, const std::string& v){ size_t p=apiSection.find(ph); if(p!=std::string::npos) apiSection.replace(p, std::strlen(ph), v); };
    apisub("__IR_ENABLED__", cfg.ir.enabled ? " checked" : "");
    apisub("__IR_TX_EXT__", cfg.ir.tx_mode==IrMode::EXTERNAL ? " selected" : "");
    apisub("__IR_TX_INT__", cfg.ir.tx_mode==IrMode::INTERNAL ? " selected" : "");
    apisub("__IR_RX_INT__", cfg.ir.rx_mode==IrMode::INTERNAL ? " selected" : "");
    apisub("__IR_RX_EXT__", cfg.ir.rx_mode==IrMode::EXTERNAL ? " selected" : "");
    apisub("__IR_TOKEN_PH__", cfg.ir.bearer_token.empty() ? "Optional" : "Saved (leave blank to keep)");
    apisub("__SPEAK_ENABLED__", cfg.speak_enabled ? " checked" : "");
    std::string page = "<!doctype html><html lang=\"en\"><meta name=\"viewport\" content=\"width=device-width\"><style>body{font-family:sans-serif;max-width:36rem;margin:2rem auto;padding:1rem;background:#10141b;color:#eef}fieldset{margin:1.2rem 0;padding:1rem;border:1px solid #445;border-radius:10px}legend{font-size:1.2rem;font-weight:bold}label{display:block;margin-top:.8rem}input,select,button{font-size:1rem;padding:.7rem;width:100%;box-sizing:border-box;background:#fff;color:#111;border:0;border-radius:4px}input[type=checkbox]{width:auto}.mode{display:flex;align-items:center;gap:.6rem}.mode input{margin:0}button{margin-top:1.5rem;background:#19b5a5;color:#fff}</style><h1>StickS3 Settings</h1><form method=\"post\" action=\"/save\"><fieldset><legend>Date &amp; Time</legend><label>NTP server</label><input name=\"ntp\" value=\"" + htmlEscape(cfg.ntp_server) + "\" required><label>Time zone</label><select name=\"timezone\">"+zones+"</select></fieldset><fieldset><legend>Connection Mode</legend><label class=\"mode\"><input id=\"modeSeparate\" type=\"checkbox\" name=\"connection_mode\" value=\"separate\""+(cfg.connection_mode=="separate"?" checked":"")+"> Separate APIs</label><label class=\"mode\"><input id=\"modeIntegrated\" type=\"checkbox\" name=\"connection_mode\" value=\"integrated\""+(cfg.connection_mode=="integrated"?" checked":"")+"> Integrated API</label></fieldset><div id=\"separateApis\">"+
        serviceBlock("stt","STT",cfg.stt,false,false,false,"none","",true)+serviceBlock("llm","LLM",cfg.llm,false,true,true,cfg.llm_agent,cfg.llm_session_id)+serviceBlock("tts","TTS",cfg.tts,true,false,false,"none","",false,true)+"</div><div id=\"integratedApi\"><fieldset><legend>Integrated API</legend><label>URL</label><input type=\"url\" name=\"int_url\" value=\""+htmlEscape(cfg.integrated.url)+"\"><label>API Key</label><input type=\"password\" name=\"int_key\" placeholder=\""+(cfg.integrated.api_key.empty()?"Not set":"Saved (leave blank to keep)")+"\"><label>User</label><input name=\"int_user\" value=\""+htmlEscape(cfg.integrated.user)+"\"><label>Session Key</label><input name=\"int_session\" value=\""+htmlEscape(cfg.integrated.session_key)+"\"><label>Device ID</label><input name=\"int_device\" value=\""+htmlEscape(cfg.integrated.device_id)+"\"><label>Voice</label><input name=\"int_voice\" value=\""+htmlEscape(cfg.integrated.voice)+"\"><label><input type=\"checkbox\" name=\"int_correct\" value=\"1\""+(cfg.integrated.correct_transcript?" checked":"")+"> Correct Transcript</label></fieldset></div>"+
        FACE_UPLOAD_HTML + bleSection + apiSection + "<button>Save</button></form>" + "<script>const ms=document.getElementById('modeSeparate'),mi=document.getElementById('modeIntegrated'),sep=document.getElementById('separateApis'),integ=document.getElementById('integratedApi'),p=document.querySelector('[name=llm_provider]'),a=document.getElementById('llmAgent'),af=document.getElementById('agentFields'),sf=document.getElementById('sessionField');const defaults={stt:{openai:{url:'https://api.openai.com/v1/audio/transcriptions',model:'gpt-4o-transcribe'},gemini:{url:'https://generativelanguage.googleapis.com/v1beta',model:'gemini-3.5-transcribe'}},llm:{openai:{url:'https://api.openai.com/v1/responses',model:'gpt-5.6-luna'},gemini:{url:'https://generativelanguage.googleapis.com/v1beta',model:'gemini-3.5-flash-lite'},anthropic:{url:'',model:''}},tts:{openai:{url:'https://api.openai.com/v1/audio/speech',model:'gpt-4o-mini-tts',voice:'marin',instructions:'Speak in a cheerful and positive tone.'},gemini:{url:'https://generativelanguage.googleapis.com/v1beta',model:'gemini-3.1-flash-tts-preview',voice:'Leda',instructions:'若い少女のアニメキャラクターのように話してください。\\n高く明るい声で、可愛らしく元気にしてください。\\n少し感情を大きめに表現し、語尾を柔らかくしてください。'}}};function bind(id){const provider=document.querySelector(`[name=${id}_provider]`),url=document.querySelector(`[name=${id}_url]`),model=document.querySelector(`[name=${id}_model]`),voice=document.querySelector(`[name=${id}_voice]`),instr=document.querySelector(`[name=${id}_instr]`),cache={};let current=provider.value;function read(){return{url:url.value,model:model.value,voice:voice?voice.value:'',instructions:instr?instr.value:''}}function write(v){url.value=v.url||'';model.value=v.model||'';if(voice)voice.value=v.voice||'';if(instr)instr.value=v.instructions||''}cache[current]=read();provider.addEventListener('change',()=>{cache[current]=read();current=provider.value;write(cache[current]||defaults[id][current]||{url:'',model:'',voice:'',instructions:''})})}bind('stt');bind('llm');bind('tts');function toggle(){sep.style.display=ms.checked?'block':'none';integ.style.display=mi.checked?'block':'none';af.style.display=p.value==='openai'?'block':'none';sf.style.display=p.value==='openai'&&a.value!=='none'?'block':'none'}function choose(e){if(e.target===ms){ms.checked=true;mi.checked=false}else{mi.checked=true;ms.checked=false}toggle()}ms.addEventListener('change',choose);mi.addEventListener('change',choose);p.addEventListener('change',toggle);a.addEventListener('change',toggle);toggle();" + FACE_UPLOAD_JS + BLE_SENSORS_JS + "</script></html>";
    httpd_resp_set_type(r,"text/html; charset=utf-8"); return httpd_resp_send(r,page.c_str(),page.size());
}

esp_err_t WebServer::save(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (settingsAccessDenied(r, self)) return ESP_OK;
    if (!authenticated(r,self)) { httpd_resp_set_status(r,"303 See Other"); httpd_resp_set_hdr(r,"Location","/"); return httpd_resp_send(r,nullptr,0); }
    if (r->content_len <= 0 || r->content_len > 16384) { httpd_resp_set_status(r,"400 Bad Request"); return httpd_resp_sendstr(r,"Invalid settings"); }
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
    updated.tts.instructions=field(body,"tts_instr");
    const auto agent=field(body,"llm_agent");if(agent=="none"||agent=="openclaw"||agent=="hermes")updated.llm_agent=agent;
    const auto session=field(body,"llm_session");if(!session.empty()||body.find("llm_session=")!=std::string::npos)updated.llm_session_id=session;
    const auto mode=field(body,"connection_mode");updated.connection_mode=mode=="integrated"?"integrated":"separate";
    updated.integrated.url=field(body,"int_url");updated.integrated.user=field(body,"int_user");updated.integrated.session_key=field(body,"int_session");updated.integrated.device_id=field(body,"int_device");updated.integrated.voice=field(body,"int_voice");updated.integrated.correct_transcript=body.find("int_correct=1")!=std::string::npos;
    const auto integrated_key=field(body,"int_key");if(!integrated_key.empty())updated.integrated.api_key=integrated_key;
    updated.es.enabled=body.find("es_enabled=1")!=std::string::npos;
    updated.es.url=field(body,"es_url");updated.es.user=field(body,"es_user");
    const auto es_pass=field(body,"es_pass");if(!es_pass.empty())updated.es.password=es_pass;
    jsonToSensors(field(body,"sensors_json"), updated.sensors);
    // IR
    updated.ir.enabled = body.find("ir_enabled=1") != std::string::npos;
    updated.ir.tx_mode = field(body,"ir_tx") == "internal" ? IrMode::INTERNAL : IrMode::EXTERNAL;
    updated.ir.rx_mode = field(body,"ir_rx") == "internal" ? IrMode::INTERNAL : IrMode::EXTERNAL;
    const auto ir_token = field(body,"ir_token");
    if (!ir_token.empty()) updated.ir.bearer_token = ir_token;
    updated.speak_enabled = body.find("speak_enabled=1") != std::string::npos;
    if(updated.ntp_server.empty()||updated.timezone.empty()){httpd_resp_set_status(r,"400 Bad Request");return httpd_resp_sendstr(r,"Invalid settings");}
    self->settings_=updated;
    if(self->save_callback_)self->save_callback_(self->save_context_,updated);
    const auto face_mode=field(body,"face_mode");
    if(face_mode=="vector"&&self->face_store_)self->face_store_->deleteAllFaces();
    httpd_resp_set_status(r,"303 See Other");httpd_resp_set_hdr(r,"Location","/settings");return httpd_resp_send(r,nullptr,0);
}
esp_err_t WebServer::upload(httpd_req_t* r) {
    auto* self = static_cast<WebServer*>(r->user_ctx);
    if (settingsAccessDenied(r, self)) return ESP_OK;
    if (!authenticated(r, self)) { httpd_resp_set_status(r,"303 See Other"); httpd_resp_set_hdr(r,"Location","/"); return httpd_resp_send(r,nullptr,0); }
    if (!self->face_store_) { httpd_resp_set_status(r,"500 Server Error"); return httpd_resp_sendstr(r,"Face store unavailable"); }
    size_t query_len = httpd_req_get_url_query_len(r) + 1;
    if (query_len > 32) { httpd_resp_set_status(r,"400 Bad Request"); return httpd_resp_sendstr(r,"Invalid request"); }
    char query[32];
    if (httpd_req_get_url_query_str(r, query, query_len) != ESP_OK) { httpd_resp_set_status(r,"400 Bad Request"); return httpd_resp_sendstr(r,"Invalid request"); }
    FaceExpression expression;
    if (!parseFaceId(field(query, "face"), expression)) { httpd_resp_set_status(r,"400 Bad Request"); return httpd_resp_sendstr(r,"Unknown face"); }
    if (r->content_len != FaceStore::FACE_BYTES) { httpd_resp_set_status(r,"400 Bad Request"); return httpd_resp_sendstr(r,"Invalid size"); }
    std::vector<uint8_t> buffer(FaceStore::FACE_BYTES);
    size_t received = 0;
    while (received < buffer.size()) {
        int n = httpd_req_recv(r, reinterpret_cast<char*>(buffer.data()) + received, buffer.size() - received);
        if (n <= 0) return ESP_FAIL;
        received += n;
    }
    if (!self->face_store_->saveFace(expression, buffer.data(), buffer.size())) { httpd_resp_set_status(r,"500 Server Error"); return httpd_resp_sendstr(r,"Save failed"); }
    return httpd_resp_sendstr(r,"OK");
}


bool WebServer::settingsAccessDenied(httpd_req_t* r, const WebServer* self) {
    if (self->settings_enabled_.load()) return false;
    httpd_resp_set_status(r, "403 Forbidden");
    httpd_resp_set_type(r, "text/plain");
    return httpd_resp_sendstr(r, "Settings screen is not active. Open Config on the device.") == ESP_OK;
}

bool WebServer::authenticated(httpd_req_t* r, const WebServer* self) {
    size_t n = httpd_req_get_hdr_value_len(r,"Cookie"); if (!n || n > 255) return false;
    char cookie[256]; if (httpd_req_get_hdr_value_str(r,"Cookie",cookie,sizeof(cookie)) != ESP_OK) return false;
    return std::strstr(cookie,("stick_session="+self->token_).c_str()) != nullptr;
}
