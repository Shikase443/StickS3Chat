#include "voice_chat.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "M5Unified.hpp"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"

class HttpReader {
public:
    explicit HttpReader(esp_http_client_handle_t client):client_(client){}
    bool readExact(void* destination,size_t length){
        auto* output=static_cast<uint8_t*>(destination);
        while(length){if(position_==available_&&!fill())return false;size_t count=std::min(length,available_-position_);std::memcpy(output,buffer_.data()+position_,count);output+=count;position_+=count;length-=count;}return true;
    }
    bool skip(size_t length){std::array<uint8_t,256> ignored{};while(length){size_t count=std::min(length,ignored.size());if(!readExact(ignored.data(),count))return false;length-=count;}return true;}
    bool readLine(std::string& line,size_t limit=262144){
        line.clear();for(;;){if(position_==available_&&!fill())return false;char value=static_cast<char>(buffer_[position_++]);if(value=='\n'){if(!line.empty()&&line.back()=='\r')line.pop_back();return true;}if(line.size()>=limit)return false;line.push_back(value);}
    }
    int readSome(void* destination,size_t capacity){
        if(!capacity)return 0;
        auto* output=static_cast<uint8_t*>(destination);
        if(position_<available_){size_t count=std::min(capacity,available_-position_);std::memcpy(output,buffer_.data()+position_,count);position_+=count;return static_cast<int>(count);}
        return esp_http_client_read(client_,reinterpret_cast<char*>(output),capacity);
    }
private:
    bool fill(){int length=esp_http_client_read(client_,reinterpret_cast<char*>(buffer_.data()),buffer_.size());if(length<=0)return false;position_=0;available_=static_cast<size_t>(length);return true;}
    esp_http_client_handle_t client_;
    std::array<uint8_t,4096> buffer_{};
    size_t position_=0,available_=0;
};

namespace {
struct HttpResult { int status=0; std::vector<uint8_t> body; };
struct Header { const char* name; std::string value; };
constexpr size_t MAX_JSON_RESPONSE=512*1024;
constexpr size_t MAX_GEMINI_AUDIO_RESPONSE=5*1024*1024;

bool endsWith(const std::string& s,const char* suffix){size_t n=std::strlen(suffix);return s.size()>=n&&s.compare(s.size()-n,n,suffix)==0;}
size_t utf8Length(uint8_t first){if((first&0x80)==0)return 1;if((first&0xE0)==0xC0)return 2;if((first&0xF0)==0xE0)return 3;if((first&0xF8)==0xF0)return 4;return 1;}
std::string geminiUrl(std::string url,const std::string& model){if(url.find(":generateContent")!=std::string::npos)return url;while(!url.empty()&&url.back()=='/')url.pop_back();return url+"/models/"+model+":generateContent";}
std::string geminiStreamUrl(std::string url,const std::string& model){url=geminiUrl(std::move(url),model);auto method=url.find(":generateContent");if(method!=std::string::npos)url.replace(method,std::strlen(":generateContent"),":streamGenerateContent");return url;}
std::string geminiInteractionsUrl(std::string url){size_t models=url.find("/models/");if(models!=std::string::npos)url.erase(models);size_t method=url.find(":generateContent");if(method!=std::string::npos)url.erase(method);while(!url.empty()&&url.back()=='/')url.pop_back();if(endsWith(url,"/interactions"))return url;return url+"/interactions";}
std::string jsonPrint(cJSON* root){char* p=cJSON_PrintUnformatted(root);std::string s=p?p:"";if(p)cJSON_free(p);return s;}
std::string jsonString(cJSON* object,const char* key){auto* v=cJSON_GetObjectItemCaseSensitive(object,key);return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";}
std::string ttsPrompt(const std::string& instructions,const std::string& text){if(instructions.empty())return text;return "# AUDIO PROFILE\n若い日本人の少女のアニメキャラクター。\n\n### DIRECTOR'S NOTES\n"+instructions+"\n\n#### TRANSCRIPT\n"+text;}
std::string base64(const uint8_t* data,size_t size){size_t n=0;mbedtls_base64_encode(nullptr,0,&n,data,size);std::string out(n,'\0');if(mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out.data()),out.size(),&n,data,size)!=0)return {};out.resize(n);return out;}
bool decode64(const std::string& text,std::vector<uint8_t>& out){size_t n=0;if(mbedtls_base64_decode(nullptr,0,&n,reinterpret_cast<const unsigned char*>(text.data()),text.size())!=MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)return false;out.resize(n);return mbedtls_base64_decode(out.data(),out.size(),&n,reinterpret_cast<const unsigned char*>(text.data()),text.size())==0?(out.resize(n),true):false;}
uint16_t get16(const uint8_t* p){return static_cast<uint16_t>(p[0]|(p[1]<<8));}
uint32_t get32(const uint8_t* p){return static_cast<uint32_t>(p[0])|(static_cast<uint32_t>(p[1])<<8)|(static_cast<uint32_t>(p[2])<<16)|(static_cast<uint32_t>(p[3])<<24);}
bool sendAll(esp_http_client_handle_t client,const void* data,size_t length){auto* bytes=static_cast<const char*>(data);while(length){int written=esp_http_client_write(client,bytes,length);if(written<=0)return false;bytes+=written;length-=written;}return true;}
bool readPartHeaders(HttpReader& reader,size_t& content_length){content_length=0;std::string line;while(reader.readLine(line)){if(line.empty())return true;constexpr const char* prefix="Content-Length:";if(line.rfind(prefix,0)==0)content_length=std::strtoul(line.c_str()+std::strlen(prefix),nullptr,10);}return false;}
esp_err_t captureUploadUrl(esp_http_client_event_t* event){
    if(event->event_id==HTTP_EVENT_ON_HEADER&&event->user_data&&event->header_key&&event->header_value){
        std::string key(event->header_key);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(key=="x-goog-upload-url")*static_cast<std::string*>(event->user_data)=event->header_value;
    }
    return ESP_OK;
}

HttpResult post(const std::string& url,const std::string& type,const uint8_t* data,size_t size,const std::vector<Header>& headers,size_t max_response=MAX_JSON_RESPONSE){
    HttpResult result; esp_http_client_config_t cfg{};cfg.url=url.c_str();cfg.method=HTTP_METHOD_POST;cfg.timeout_ms=120000;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.buffer_size=4096;cfg.buffer_size_tx=4096;
    auto client=esp_http_client_init(&cfg);if(!client)return result;
    esp_http_client_set_header(client,"Content-Type",type.c_str());for(const auto& h:headers)if(!h.value.empty())esp_http_client_set_header(client,h.name,h.value.c_str());
    if(esp_http_client_open(client,size)!=ESP_OK){esp_http_client_cleanup(client);return result;}
    size_t sent=0;while(sent<size){int n=esp_http_client_write(client,reinterpret_cast<const char*>(data+sent),size-sent);if(n<=0){esp_http_client_close(client);esp_http_client_cleanup(client);return result;}sent+=n;}
    int64_t content_length=esp_http_client_fetch_headers(client);result.status=esp_http_client_get_status_code(client);if(content_length>0){size_t wanted=static_cast<size_t>(content_length);size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);if(wanted>max_response||wanted+65536>largest){result.status=0;esp_http_client_close(client);esp_http_client_cleanup(client);return result;}result.body.reserve(wanted);}std::array<uint8_t,4096> buf{};for(;;){int n=esp_http_client_read(client,reinterpret_cast<char*>(buf.data()),buf.size());if(n<=0)break;size_t required=result.body.size()+static_cast<size_t>(n);if(required>max_response){result.status=0;result.body.clear();break;}if(required>result.body.capacity()){size_t next=std::min(max_response,std::max(required,result.body.capacity()+65536));if(next+result.body.capacity()+32768>heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)){result.status=0;result.body.clear();break;}result.body.reserve(next);}result.body.insert(result.body.end(),buf.begin(),buf.begin()+n);}
    esp_http_client_close(client);esp_http_client_cleanup(client);return result;
}
HttpResult postJson(const std::string& url,const std::string& body,const std::vector<Header>& headers,size_t max_response=MAX_JSON_RESPONSE){return post(url,"application/json",reinterpret_cast<const uint8_t*>(body.data()),body.size(),headers,max_response);}
std::vector<Header> auth(const ServiceSettings& s){if(s.api_key.empty())return {};if(s.provider=="gemini")return {{"x-goog-api-key",s.api_key}};if(s.provider=="anthropic")return {{"x-api-key",s.api_key},{"anthropic-version","2023-06-01"}};return {{"Authorization","Bearer "+s.api_key}};}
cJSON* parse(const HttpResult& r){if(r.status<200||r.status>=300||r.body.empty())return nullptr;return cJSON_ParseWithLength(reinterpret_cast<const char*>(r.body.data()),r.body.size());}
std::string geminiText(cJSON* root){
    std::string text,words;auto* candidates=cJSON_GetObjectItem(root,"candidates");if(!cJSON_IsArray(candidates))return {};
    cJSON* candidate=nullptr;cJSON_ArrayForEach(candidate,candidates){auto* content=cJSON_GetObjectItem(candidate,"content");auto* parts=content?cJSON_GetObjectItem(content,"parts"):nullptr;if(!cJSON_IsArray(parts))continue;
        cJSON* part=nullptr;cJSON_ArrayForEach(part,parts){auto value=jsonString(part,"text");if(!value.empty())text+=value;auto* transcription=cJSON_GetObjectItem(part,"audioTranscription");auto* list=transcription?cJSON_GetObjectItem(transcription,"words"):nullptr;if(!cJSON_IsArray(list))continue;
            cJSON* word=nullptr;cJSON_ArrayForEach(word,list){auto value=jsonString(word,"word");if(value.empty())continue;bool separate=!words.empty()&&static_cast<unsigned char>(words.back())<0x80&&static_cast<unsigned char>(value.front())<0x80&&std::isalnum(static_cast<unsigned char>(words.back()))&&std::isalnum(static_cast<unsigned char>(value.front()));if(separate)words.push_back(' ');words+=value;}
        }
    }
    return !text.empty()?text:words;
}
std::string interactionText(cJSON* root){
    auto direct=jsonString(root,"output_text");if(!direct.empty())return direct;std::string out;
    for(const char* list_name:{"steps","output","outputs"}){auto* list=cJSON_GetObjectItem(root,list_name);if(!cJSON_IsArray(list))continue;cJSON* step=nullptr;cJSON_ArrayForEach(step,list){auto* content=cJSON_GetObjectItem(step,"content");if(!cJSON_IsArray(content))continue;cJSON* part=nullptr;cJSON_ArrayForEach(part,content){auto text=jsonString(part,"text");if(!text.empty())out+=text;}}if(!out.empty())break;}
    return out;
}
bool uploadGeminiAudio(const ServiceSettings& service,const std::vector<uint8_t>& wav_data,std::string& uri,int& status){
    uri.clear();status=0;std::string upload_url;const std::string metadata="{\"file\":{\"displayName\":\"StickS3 recording\"}}";
    esp_http_client_config_t config{};config.url="https://generativelanguage.googleapis.com/upload/v1beta/files";config.method=HTTP_METHOD_POST;config.timeout_ms=120000;config.crt_bundle_attach=esp_crt_bundle_attach;config.buffer_size=4096;config.buffer_size_tx=4096;config.event_handler=captureUploadUrl;config.user_data=&upload_url;
    auto client=esp_http_client_init(&config);if(!client)return false;
    std::string length=std::to_string(wav_data.size());esp_http_client_set_header(client,"Content-Type","application/json");esp_http_client_set_header(client,"x-goog-api-key",service.api_key.c_str());esp_http_client_set_header(client,"X-Goog-Upload-Protocol","resumable");esp_http_client_set_header(client,"X-Goog-Upload-Command","start");esp_http_client_set_header(client,"X-Goog-Upload-Header-Content-Length",length.c_str());esp_http_client_set_header(client,"X-Goog-Upload-Header-Content-Type","audio/wav");
    bool started=esp_http_client_open(client,metadata.size())==ESP_OK&&sendAll(client,metadata.data(),metadata.size())&&esp_http_client_fetch_headers(client)>=0;status=esp_http_client_get_status_code(client);std::array<char,256> drain{};while(started&&esp_http_client_read(client,drain.data(),drain.size())>0){}esp_http_client_close(client);esp_http_client_cleanup(client);
    if(!started||status<200||status>=300||upload_url.empty())return false;
    std::vector<Header> headers{{"X-Goog-Upload-Offset","0"},{"X-Goog-Upload-Command","upload, finalize"}};auto result=post(upload_url,"audio/wav",wav_data.data(),wav_data.size(),headers,65536);status=result.status;auto* root=parse(result);if(!root)return false;auto* file=cJSON_GetObjectItem(root,"file");uri=file?jsonString(file,"uri"):"";cJSON_Delete(root);return !uri.empty();
}
std::string responsesText(cJSON* root){
    auto direct=jsonString(root,"output_text");if(!direct.empty())return direct;
    auto* output=cJSON_GetObjectItem(root,"output");if(!cJSON_IsArray(output))return {};
    cJSON* item=nullptr;cJSON_ArrayForEach(item,output){auto* content=cJSON_GetObjectItem(item,"content");if(!cJSON_IsArray(content))continue;cJSON* part=nullptr;cJSON_ArrayForEach(part,content){auto text=jsonString(part,"text");if(!text.empty())return text;}}
    return {};
}
void put32(std::vector<uint8_t>& v,size_t p,uint32_t x){for(int i=0;i<4;++i)v[p+i]=x>>(8*i);}void put16(std::vector<uint8_t>& v,size_t p,uint16_t x){v[p]=x;v[p+1]=x>>8;}
std::vector<uint8_t> wav(const int16_t* pcm,size_t samples){std::vector<uint8_t> out(44+samples*2);std::memcpy(out.data(),"RIFF",4);put32(out,4,36+samples*2);std::memcpy(out.data()+8,"WAVEfmt ",8);put32(out,16,16);put16(out,20,1);put16(out,22,1);put32(out,24,16000);put32(out,28,32000);put16(out,32,2);put16(out,34,16);std::memcpy(out.data()+36,"data",4);put32(out,40,samples*2);std::memcpy(out.data()+44,pcm,samples*2);return out;}
struct PsramDeleter{void operator()(int16_t* value)const{heap_caps_free(value);}};
struct ByteDeleter{void operator()(uint8_t* value)const{heap_caps_free(value);}};
}

bool VoiceChat::begin(){
    speak_queue_=xQueueCreate(4,sizeof(std::string*));
    if(!speak_queue_)return false;
    return xTaskCreatePinnedToCore(taskEntry,"voice_api",24576,this,4,&task_,1)==pdPASS;
}
bool VoiceChat::speak(const std::string& text, const Settings& settings){
    if(!speak_queue_||text.empty())return false;
    {std::lock_guard<std::mutex> lock(mutex_);pending_settings_=settings;}
    auto* heap_text=new std::string(text);
    if(xQueueSend(speak_queue_,&heap_text,0)!=pdTRUE){delete heap_text;return false;}
    xTaskNotifyGive(task_);
    return true;
}
void VoiceChat::tick(){tickCaption();}
void VoiceChat::clearHistory(){history_.clear();}
std::string VoiceChat::message()const{std::lock_guard<std::mutex> lock(mutex_);return message_;}
std::string VoiceChat::caption()const{std::lock_guard<std::mutex> lock(mutex_);return caption_;}
void VoiceChat::setState(VoiceState s,const std::string& m){std::lock_guard<std::mutex> lock(mutex_);message_=m;state_.store(s);}
void VoiceChat::fail(const std::string& m){stopCaption();setState(VoiceState::ERROR,m);while(state()==VoiceState::ERROR){vTaskDelay(pdMS_TO_TICKS(20));}}
void VoiceChat::startCaption(const std::string& text){std::lock_guard<std::mutex> lock(mutex_);caption_target_=text;caption_.clear();caption_active_=!text.empty();caption_playback_finished_=false;caption_next_us_=esp_timer_get_time();caption_clear_us_=0;}
void VoiceChat::advanceCaption(){std::lock_guard<std::mutex> lock(mutex_);if(caption_.size()>=caption_target_.size())return;size_t p=caption_.size(),n=utf8Length(static_cast<uint8_t>(caption_target_[p]));caption_.append(caption_target_,p,std::min(n,caption_target_.size()-p));}
void VoiceChat::finishCaption(){std::lock_guard<std::mutex> lock(mutex_);caption_playback_finished_=true;if(!caption_active_)caption_clear_us_=esp_timer_get_time()+5000000;}
void VoiceChat::stopCaption(){std::lock_guard<std::mutex> lock(mutex_);caption_active_=false;caption_playback_finished_=true;caption_clear_us_=esp_timer_get_time()+5000000;}
void VoiceChat::tickCaption(){
    std::lock_guard<std::mutex> lock(mutex_);int64_t now=esp_timer_get_time();
    if(caption_active_&&now>=caption_next_us_){size_t p=caption_.size(),n=utf8Length(static_cast<uint8_t>(caption_target_[p]));caption_.append(caption_target_,p,std::min(n,caption_target_.size()-p));caption_next_us_=now+200000;if(caption_.size()>=caption_target_.size()){caption_active_=false;if(caption_playback_finished_)caption_clear_us_=now+5000000;}}
    if(caption_clear_us_&&now>=caption_clear_us_){caption_.clear();caption_target_.clear();caption_clear_us_=0;}
}

bool VoiceChat::startRecording(){
    cancel_requested_.store(false);
    M5.Speaker.end();M5.Mic.end();auto cfg=M5.Mic.config();cfg.input_channel=m5::input_only_left;cfg.sample_rate=SAMPLE_RATE;cfg.magnification=std::min<unsigned>(255,cfg.magnification*2);M5.Mic.config(cfg);if(!M5.Mic.begin())return false;
    startCaption({});
    if(recording_){heap_caps_free(recording_);}
    recording_=static_cast<int16_t*>(heap_caps_malloc(MAX_SAMPLES*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    recording_samples_=0;
    if(!recording_){M5.Mic.end();return false;}
    capture_=static_cast<int16_t*>(heap_caps_malloc(BLOCK_SAMPLES*2*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!capture_){heap_caps_free(recording_);recording_=nullptr;M5.Mic.end();return false;}head_=pending_count_=submitted_=0;release_seen_=false;setState(VoiceState::RECORDING,"Recording...");return true;
}
void VoiceChat::pumpRecording(bool held){
    if(!held) release_seen_=true;
    if(submitted_>=MAX_SAMPLES) release_seen_=true;
    size_t active=M5.Mic.isRecording();
    if(active<=pending_count_){size_t done=pending_count_-active;while(done--){auto* b=pending_[head_];size_t room=MAX_SAMPLES-recording_samples_;size_t n=std::min(room,BLOCK_SAMPLES);std::memcpy(recording_+recording_samples_,b,n*sizeof(int16_t));recording_samples_+=n;head_=(head_+1)%2;--pending_count_;}}
    while(!release_seen_&&pending_count_<2&&submitted_+BLOCK_SAMPLES<=MAX_SAMPLES){auto* b=capture_+((head_+pending_count_)%2)*BLOCK_SAMPLES;if(!M5.Mic.record(b,BLOCK_SAMPLES,SAMPLE_RATE,false)){release_seen_=true;break;}pending_[(head_+pending_count_)%2]=b;++pending_count_;submitted_+=BLOCK_SAMPLES;}
    if(release_seen_&&pending_count_==0)finishRecording();
}
void VoiceChat::finishRecording(){M5.Mic.end();heap_caps_free(capture_);capture_=nullptr;if(recording_samples_<SAMPLE_RATE/4){setState(VoiceState::IDLE);heap_caps_free(recording_);recording_=nullptr;recording_samples_=0;return;}std::string conn_mode;{std::lock_guard<std::mutex> lock(mutex_);conn_mode=pending_settings_.connection_mode;}if(conn_mode=="integrated")setState(VoiceState::LLM_PROCESSING,"Processing...");else setState(VoiceState::STT_PROCESSING,"Recognizing...");xTaskNotifyGive(task_);}
void VoiceChat::update(bool pressed,bool released,bool wifi,const Settings& settings){
    auto s=state();if(s==VoiceState::IDLE&&pressed){if(!wifi){setState(VoiceState::ERROR,"Wi-Fi disconnected");return;}{std::lock_guard<std::mutex> lock(mutex_);pending_settings_=settings;}if(!startRecording())setState(VoiceState::ERROR,"Recording failed");}
    else if(s==VoiceState::RECORDING)pumpRecording(!released&&M5.BtnA.isPressed());
    else if(s==VoiceState::PLAYING&&pressed)cancel_requested_.store(true);
    else if(s==VoiceState::ERROR&&pressed){setState(VoiceState::IDLE);}
}
void VoiceChat::taskEntry(void* arg){static_cast<VoiceChat*>(arg)->taskLoop();}
void VoiceChat::taskLoop(){
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        Settings cfg;{std::lock_guard<std::mutex> lock(mutex_);cfg=pending_settings_;}
        // 録音（ボタン操作による会話）を処理
        if(recording_ != nullptr){
            std::unique_ptr<int16_t,PsramDeleter> pcm(recording_);
            size_t pcm_samples=recording_samples_;
            recording_=nullptr;recording_samples_=0;
            std::string user,answer;
            if(cfg.connection_mode=="integrated"){
                setState(VoiceState::LLM_PROCESSING,"Processing...");std::string integ_error;if(!integrated(cfg,pcm.get(),pcm_samples,user,answer,integ_error)){if(cancel_requested_.exchange(false)){stopCaption();setState(VoiceState::IDLE);continue;}fail("Integrated: "+integ_error);continue;}pcm.reset();
                if(cancel_requested_.exchange(false))stopCaption();else finishCaption();
                setState(VoiceState::IDLE);stack_low_water_.store(uxTaskGetStackHighWaterMark(nullptr));free_heap_.store(heap_caps_get_free_size(MALLOC_CAP_8BIT));continue;
            }
            std::string stt_error;setState(VoiceState::STT_PROCESSING,"Recognizing...");if(!transcribe(cfg,pcm.get(),pcm_samples,user,stt_error)){fail(stt_error.empty()?"STT failed":stt_error);continue;}pcm.reset();
            setState(VoiceState::LLM_PROCESSING,"Thinking...");if(!complete(cfg,user,answer)){fail("LLM failed");continue;}
            std::string tts_error;setState(VoiceState::TTS_PROCESSING,"Generating voice...");startCaption(answer);if(!synthesizeAndPlay(cfg,answer,tts_error)){if(cancel_requested_.exchange(false)){stopCaption();setState(VoiceState::IDLE);continue;}fail(tts_error.empty()?"TTS failed":tts_error);continue;}
            if(cancel_requested_.exchange(false))stopCaption();else finishCaption();
            setState(VoiceState::IDLE);
            stack_low_water_.store(uxTaskGetStackHighWaterMark(nullptr));free_heap_.store(heap_caps_get_free_size(MALLOC_CAP_8BIT));
            continue;
        }
        // 発話要求（API）を処理
        std::string* text_ptr;
        while(xQueueReceive(speak_queue_,&text_ptr,0)==pdTRUE){
            std::string text=*text_ptr;delete text_ptr;
            if(text.empty())continue;
            setState(VoiceState::TTS_PROCESSING,"Speaking...");
            startCaption(text);
            std::string tts_error;
            bool played=cfg.connection_mode=="integrated"?integratedSpeak(cfg,text,tts_error):synthesizeAndPlay(cfg,text,tts_error);
            if(!played){
                if(cancel_requested_.exchange(false)){stopCaption();setState(VoiceState::IDLE);continue;}
                fail(tts_error.empty()?"TTS failed":tts_error);continue;
            }
            if(cancel_requested_.exchange(false))stopCaption();else finishCaption();
            setState(VoiceState::IDLE);
        }
    }
}

bool VoiceChat::transcribe(const Settings& cfg,const int16_t* pcm,size_t pcm_samples,std::string& out,std::string& error){
    error.clear();const auto& s=cfg.stt;if(s.url.empty()||s.model.empty()){error="STT config error";return false;}auto w=wav(pcm,pcm_samples);HttpResult r;
    if(s.provider=="gemini"){
        const bool transcribe_model=s.model.find("transcribe")!=std::string::npos;
        std::string language;if(!s.language.empty()&&s.language!="Auto")language=s.language=="ja"?"ja-JP":s.language=="en"?"en-US":s.language;
        if(transcribe_model){
            std::string file_uri;int upload_status=0;if(!uploadGeminiAudio(s,w,file_uri,upload_status)){error=upload_status?"STT upload HTTP "+std::to_string(upload_status):"STT upload error";return false;}
            auto* request=cJSON_CreateObject();cJSON_AddStringToObject(request,"model",s.model.c_str());auto* input=cJSON_AddArrayToObject(request,"input");auto* audio=cJSON_CreateObject();cJSON_AddStringToObject(audio,"type","audio");cJSON_AddStringToObject(audio,"uri",file_uri.c_str());cJSON_AddStringToObject(audio,"mime_type","audio/wav");cJSON_AddItemToArray(input,audio);
            if(!language.empty()){auto* generation=cJSON_AddObjectToObject(request,"generation_config");auto* transcription=cJSON_AddObjectToObject(generation,"transcription_config");auto* codes=cJSON_AddArrayToObject(transcription,"language_codes");cJSON_AddItemToArray(codes,cJSON_CreateString(language.c_str()));}
            auto body=jsonPrint(request);cJSON_Delete(request);r=postJson(geminiInteractionsUrl(s.url),body,auth(s));if(r.status<200||r.status>=300){error=r.status?"STT HTTP "+std::to_string(r.status):"STT network error";return false;}auto* response=parse(r);if(!response){error="STT response error";return false;}out=interactionText(response);auto interaction_status=jsonString(response,"status");cJSON_Delete(response);if(out.empty()){error=interaction_status.empty()?"STT empty result":"STT "+interaction_status;return false;}return true;
        }
        auto* root=cJSON_CreateObject();
        auto* contents=cJSON_AddArrayToObject(root,"contents");auto* item=cJSON_CreateObject();cJSON_AddItemToArray(contents,item);auto* parts=cJSON_AddArrayToObject(item,"parts");
        std::string instruction="Transcribe this audio. Return only the transcript.";if(!language.empty())instruction+=" The spoken language is "+language+".";auto* prompt=cJSON_CreateObject();cJSON_AddStringToObject(prompt,"text",instruction.c_str());cJSON_AddItemToArray(parts,prompt);
        auto b=base64(w.data(),w.size());if(b.empty()){cJSON_Delete(root);error="STT memory error";return false;}auto* part=cJSON_CreateObject();auto* data=cJSON_AddObjectToObject(part,"inlineData");cJSON_AddStringToObject(data,"mimeType","audio/wav");cJSON_AddStringToObject(data,"data",b.c_str());cJSON_AddItemToArray(parts,part);
        auto body=jsonPrint(root);cJSON_Delete(root);r=postJson(geminiUrl(s.url,s.model),body,auth(s));
        if(r.status<200||r.status>=300){error=r.status?"STT HTTP "+std::to_string(r.status):"STT network error";return false;}
        auto* j=parse(r);if(!j){error="STT response error";return false;}out=geminiText(j);cJSON_Delete(j);
    }
    else{const char* boundary="----StickS3Boundary";std::string prefix="--"+std::string(boundary)+"\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n"+s.model+"\r\n";if(!s.language.empty()&&s.language!="Auto")prefix+="--"+std::string(boundary)+"\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n"+s.language+"\r\n";prefix+="--"+std::string(boundary)+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";std::string suffix="\r\n--"+std::string(boundary)+"--\r\n";std::vector<uint8_t> body;body.reserve(prefix.size()+w.size()+suffix.size());body.insert(body.end(),prefix.begin(),prefix.end());body.insert(body.end(),w.begin(),w.end());body.insert(body.end(),suffix.begin(),suffix.end());r=post(s.url,"multipart/form-data; boundary="+std::string(boundary),body.data(),body.size(),auth(s));if(r.status<200||r.status>=300){error=r.status?"STT HTTP "+std::to_string(r.status):"STT network error";return false;}auto* j=parse(r);if(!j){error="STT response error";return false;}out=jsonString(j,"text");cJSON_Delete(j);}
    if(out.empty()){error="STT empty result";return false;}return true;}

bool VoiceChat::integrated(const Settings& cfg,const int16_t* pcm,size_t pcm_samples,std::string& transcript,std::string& answer,std::string& error){
    error.clear();const auto& s=cfg.integrated;if(s.url.empty()){error="no URL";return false;}
    if(configured_session_!=s.session_key){configured_session_=s.session_key;integrated_session_=s.session_key;}
    constexpr const char* boundary="----StickS3Integrated7MA4";
    auto field=[&](const char* name,const std::string& value){return value.empty()?std::string{}:"--"+std::string(boundary)+"\r\nContent-Disposition: form-data; name=\""+name+"\"\r\n\r\n"+value+"\r\n";};
    std::string prefix=field("correctTranscript",s.correct_transcript?"true":"false")+field("user",s.user)+field("sessionKey",integrated_session_)+field("deviceId",s.device_id)+field("resetSession","false")+field("voice",s.voice)+"--"+boundary+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    std::string suffix="\r\n--"+std::string(boundary)+"--\r\n";std::array<uint8_t,44> header{};std::memcpy(header.data(),"RIFF",4);for(int i=0;i<4;++i)header[4+i]=(36+pcm_samples*2)>>(8*i);std::memcpy(header.data()+8,"WAVEfmt ",8);header[16]=16;header[20]=1;header[22]=1;for(int i=0;i<4;++i){header[24+i]=SAMPLE_RATE>>(8*i);header[28+i]=(SAMPLE_RATE*2)>>(8*i);}header[32]=2;header[34]=16;std::memcpy(header.data()+36,"data",4);for(int i=0;i<4;++i){header[40+i]=(pcm_samples*2)>>(8*i);}
    esp_http_client_config_t config{};config.url=s.url.c_str();config.method=HTTP_METHOD_POST;config.timeout_ms=300000;config.crt_bundle_attach=esp_crt_bundle_attach;config.buffer_size=4096;config.buffer_size_tx=4096;auto client=esp_http_client_init(&config);if(!client){error="init fail";return false;}std::string type="multipart/form-data; boundary="+std::string(boundary);esp_http_client_set_header(client,"Content-Type",type.c_str());std::string authorization;if(!s.api_key.empty()){authorization="Bearer "+s.api_key;esp_http_client_set_header(client,"Authorization",authorization.c_str());}
    size_t request_size=prefix.size()+header.size()+pcm_samples*2+suffix.size();esp_err_t open_err=esp_http_client_open(client,request_size);if(open_err!=ESP_OK){
        const int socket_errno=esp_http_client_get_errno(client);
        int mbedtls_error=0;int verify_flags=0;
        const esp_err_t tls_error=esp_http_client_get_and_clear_last_tls_error(client,&mbedtls_error,&verify_flags);
        constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
        const size_t free_internal=heap_caps_get_free_size(caps);
        const size_t largest_internal=heap_caps_get_largest_free_block(caps);
        char diagnostic[160];
        std::snprintf(diagnostic,sizeof(diagnostic),"HTTP:%X TLS:%X\nM:%d F:%X\nerrno:%d\nI:%u B:%u",
            static_cast<unsigned>(open_err),static_cast<unsigned>(tls_error),mbedtls_error,
            static_cast<unsigned>(verify_flags),socket_errno,
            static_cast<unsigned>(free_internal),static_cast<unsigned>(largest_internal));
        error=diagnostic;esp_http_client_cleanup(client);return false;}bool ok=sendAll(client,prefix.data(),prefix.size())&&sendAll(client,header.data(),header.size())&&sendAll(client,pcm,pcm_samples*2)&&sendAll(client,suffix.data(),suffix.size());if(!ok){error="send fail";esp_http_client_cleanup(client);return false;}int fh=esp_http_client_fetch_headers(client);if(fh<0){error="hdr:"+std::to_string(fh);esp_http_client_cleanup(client);return false;}int status=esp_http_client_get_status_code(client);if(status<200||status>=300){error="HTTP "+std::to_string(status);esp_http_client_cleanup(client);return false;}
    HttpReader reader(client);std::string line;size_t ignored=0;if(!reader.readLine(line)){error="read boundary";esp_http_client_cleanup(client);return false;}if(line.rfind("--",0)!=0){error="bad boundary";esp_http_client_cleanup(client);return false;}if(!readPartHeaders(reader,ignored)){error="meta headers";esp_http_client_cleanup(client);return false;}std::string metadata;bool found=false;while(reader.readLine(line)){if(line.rfind("--",0)==0){found=true;break;}if(metadata.size()+line.size()+1>MAX_JSON_RESPONSE){error="meta too large";esp_http_client_cleanup(client);return false;}metadata+=line;metadata.push_back('\n');}if(!found){error="no meta end";esp_http_client_cleanup(client);return false;}auto* json=cJSON_ParseWithLength(metadata.data(),metadata.size());if(!json){error="JSON parse";esp_http_client_cleanup(client);return false;}transcript=jsonString(json,"transcript");answer=jsonString(json,"answer");auto session=jsonString(json,"sessionKey");cJSON_Delete(json);if(!session.empty())integrated_session_=session;
    size_t audio_remaining=0;if(!readPartHeaders(reader,audio_remaining)){error="audio headers";esp_http_client_cleanup(client);return false;}if(!audio_remaining){error="no audio len";esp_http_client_cleanup(client);return false;}startCaption(answer.empty()?transcript:answer);bool played=playStream(reader,audio_remaining,true);if(!played)error="play fail";esp_http_client_cleanup(client);return played;
}

bool VoiceChat::integratedSpeak(const Settings& cfg,const std::string& text,std::string& error){
    error.clear();const auto& s=cfg.integrated;if(s.url.empty()){error="no URL";return false;}
    std::string speech_url=s.url;auto slash=speech_url.rfind('/');if(slash!=std::string::npos)speech_url=speech_url.substr(0,slash+1)+"speech";
    auto* root=cJSON_CreateObject();if(!root){error="json alloc";return false;}cJSON_AddStringToObject(root,"text",text.c_str());if(!s.voice.empty())cJSON_AddStringToObject(root,"voice",s.voice.c_str());cJSON_AddStringToObject(root,"response_format","wav");auto body=jsonPrint(root);cJSON_Delete(root);if(body.empty()){error="json print";return false;}
    esp_http_client_config_t config{};config.url=speech_url.c_str();config.method=HTTP_METHOD_POST;config.timeout_ms=300000;config.crt_bundle_attach=esp_crt_bundle_attach;config.buffer_size=4096;config.buffer_size_tx=4096;auto client=esp_http_client_init(&config);if(!client){error="init fail";return false;}esp_http_client_set_header(client,"Content-Type","application/json");if(!s.api_key.empty()){std::string auth="Bearer "+s.api_key;esp_http_client_set_header(client,"Authorization",auth.c_str());}
    bool ok=esp_http_client_open(client,body.size())==ESP_OK&&sendAll(client,body.data(),body.size());if(!ok||esp_http_client_fetch_headers(client)<0){esp_http_client_cleanup(client);error="HTTP fail";return false;}int status=esp_http_client_get_status_code(client);if(status<200||status>=300){esp_http_client_cleanup(client);error="HTTP "+std::to_string(status);return false;}
    std::vector<uint8_t> audio;auto* buf=static_cast<uint8_t*>(heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!buf){esp_http_client_cleanup(client);error="buf alloc";return false;}for(;;){int n=esp_http_client_read(client,reinterpret_cast<char*>(buf),4096);if(n<=0)break;audio.insert(audio.end(),buf,buf+n);}heap_caps_free(buf);esp_http_client_cleanup(client);
    if(audio.empty()){error="empty audio";return false;}
    return play(audio,false);
}

bool VoiceChat::complete(const Settings& cfg,const std::string& user,std::string& out){const auto& s=cfg.llm;if(s.url.empty()||s.model.empty())return false;auto* root=cJSON_CreateObject();cJSON_AddStringToObject(root,"model",s.model.c_str());
    const bool external_session=s.provider=="openai"&&cfg.llm_agent!="none"&&!cfg.llm_session_id.empty();
    if(s.provider=="gemini"){auto* generation=cJSON_AddObjectToObject(root,"generationConfig");cJSON_AddNumberToObject(generation,"maxOutputTokens",256);auto* contents=cJSON_AddArrayToObject(root,"contents");for(const auto& h:history_){auto* msg=cJSON_CreateObject();cJSON_AddStringToObject(msg,"role",h.role=="assistant"?"model":"user");auto* parts=cJSON_AddArrayToObject(msg,"parts");auto* p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",h.text.c_str());cJSON_AddItemToArray(parts,p);cJSON_AddItemToArray(contents,msg);}auto* msg=cJSON_CreateObject();cJSON_AddStringToObject(msg,"role","user");auto* parts=cJSON_AddArrayToObject(msg,"parts");auto* p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",user.c_str());cJSON_AddItemToArray(parts,p);cJSON_AddItemToArray(contents,msg);}
    else{auto* messages=cJSON_AddArrayToObject(root,"messages");if(s.provider=="openai"&&!endsWith(s.url,"/responses")){auto* sys=cJSON_CreateObject();cJSON_AddStringToObject(sys,"role","system");cJSON_AddStringToObject(sys,"content","256トークンで要約して回答");cJSON_AddItemToArray(messages,sys);}if(!external_session)for(const auto& h:history_){auto* m=cJSON_CreateObject();cJSON_AddStringToObject(m,"role",h.role.c_str());cJSON_AddStringToObject(m,"content",h.text.c_str());cJSON_AddItemToArray(messages,m);}auto* m=cJSON_CreateObject();cJSON_AddStringToObject(m,"role","user");cJSON_AddStringToObject(m,"content",user.c_str());cJSON_AddItemToArray(messages,m);if(s.provider=="anthropic")cJSON_AddNumberToObject(root,"max_tokens",256);else if(endsWith(s.url,"/responses")){cJSON_AddStringToObject(root,"instructions","256トークンで要約して回答");cJSON_DetachItemFromObject(root,"messages");cJSON_AddItemToObject(root,"input",messages);}}
    auto body=jsonPrint(root);cJSON_Delete(root);auto target=s.provider=="gemini"?geminiUrl(s.url,s.model):s.url;auto headers=auth(s);if(external_session&&cfg.llm_agent=="openclaw")headers.push_back({"x-openclaw-session-key",cfg.llm_session_id});else if(external_session&&cfg.llm_agent=="hermes")headers.push_back({"X-Hermes-Session-Id",cfg.llm_session_id});auto r=postJson(target,body,headers);auto* j=parse(r);if(!j)return false;
    if(s.provider=="gemini")out=geminiText(j);else if(s.provider=="anthropic"){auto* a=cJSON_GetObjectItem(j,"content");auto* p=cJSON_IsArray(a)?cJSON_GetArrayItem(a,0):nullptr;out=p?jsonString(p,"text"):"";}else if(endsWith(s.url,"/responses")){out=responsesText(j);}else{auto* a=cJSON_GetObjectItem(j,"choices");auto* c=cJSON_IsArray(a)?cJSON_GetArrayItem(a,0):nullptr;auto* m=c?cJSON_GetObjectItem(c,"message"):nullptr;out=m?jsonString(m,"content"):"";}cJSON_Delete(j);if(out.empty())return false;if(!external_session){auto compact=[](const std::string& text){constexpr size_t limit=6144;if(text.size()<=limit)return text;size_t start=text.size()-limit;while(start<text.size()&&(static_cast<uint8_t>(text[start])&0xC0)==0x80)++start;return text.substr(start);};history_.push_back({"user",compact(user)});history_.push_back({"assistant",compact(out)});size_t bytes=0;for(const auto& turn:history_)bytes+=turn.text.size();while(history_.size()>6||bytes>12288){bytes-=history_.front().text.size();history_.pop_front();}}return true;}

bool VoiceChat::synthesizeAndPlay(const Settings& cfg,const std::string& text,std::string& error){
    error.clear();const auto& s=cfg.tts;if(s.url.empty()||s.model.empty()){error="TTS config error";return false;}
    if(s.provider=="gemini")return synthesizeGeminiAndPlay(cfg,text,error);
    auto* root=cJSON_CreateObject();if(!root)return false;cJSON_AddStringToObject(root,"model",s.model.c_str());cJSON_AddStringToObject(root,"input",text.c_str());cJSON_AddStringToObject(root,"voice",s.voice.empty()?"alloy":s.voice.c_str());if(!s.instructions.empty())cJSON_AddStringToObject(root,"instructions",s.instructions.c_str());cJSON_AddStringToObject(root,"response_format","wav");auto body=jsonPrint(root);cJSON_Delete(root);if(body.empty())return false;
    esp_http_client_config_t config{};config.url=s.url.c_str();config.method=HTTP_METHOD_POST;config.timeout_ms=300000;config.crt_bundle_attach=esp_crt_bundle_attach;config.buffer_size=4096;config.buffer_size_tx=4096;auto client=esp_http_client_init(&config);if(!client){error="TTS network error";return false;}esp_http_client_set_header(client,"Content-Type","application/json");auto headers=auth(s);for(const auto& h:headers)if(!h.value.empty())esp_http_client_set_header(client,h.name,h.value.c_str());bool ok=esp_http_client_open(client,body.size())==ESP_OK&&sendAll(client,body.data(),body.size());if(!ok||esp_http_client_fetch_headers(client)<0){esp_http_client_cleanup(client);error="TTS network error";return false;}int status=esp_http_client_get_status_code(client);if(status<200||status>=300){esp_http_client_cleanup(client);error="TTS HTTP "+std::to_string(status);return false;}HttpReader reader(client);bool played=playStream(reader,0,false);esp_http_client_cleanup(client);if(!played)error="TTS playback error";return played;
}

bool VoiceChat::synthesizeGeminiAndPlay(const Settings& cfg,const std::string& text,std::string& error){
    const auto& s=cfg.tts;auto* root=cJSON_CreateObject();if(!root){error="TTS memory error";return false;}
    auto* contents=cJSON_AddArrayToObject(root,"contents");auto* item=cJSON_CreateObject();cJSON_AddItemToArray(contents,item);auto* parts=cJSON_AddArrayToObject(item,"parts");auto* part=cJSON_CreateObject();cJSON_AddStringToObject(part,"text",ttsPrompt(s.instructions,text).c_str());cJSON_AddItemToArray(parts,part);
    auto* generation=cJSON_AddObjectToObject(root,"generationConfig");auto* modalities=cJSON_AddArrayToObject(generation,"responseModalities");cJSON_AddItemToArray(modalities,cJSON_CreateString("AUDIO"));auto* speech=cJSON_AddObjectToObject(generation,"speechConfig");auto* voice=cJSON_AddObjectToObject(speech,"voiceConfig");auto* prebuilt=cJSON_AddObjectToObject(voice,"prebuiltVoiceConfig");cJSON_AddStringToObject(prebuilt,"voiceName",s.voice.empty()?"Kore":s.voice.c_str());
    auto body=jsonPrint(root);cJSON_Delete(root);if(body.empty()){error="TTS memory error";return false;}

    auto target=geminiStreamUrl(s.url,s.model);esp_http_client_config_t config{};config.url=target.c_str();config.method=HTTP_METHOD_POST;config.timeout_ms=300000;config.crt_bundle_attach=esp_crt_bundle_attach;config.buffer_size=4096;config.buffer_size_tx=4096;
    auto client=esp_http_client_init(&config);if(!client){error="TTS network error";return false;}esp_http_client_set_header(client,"Content-Type","application/json");auto headers=auth(s);for(const auto& header:headers)if(!header.value.empty())esp_http_client_set_header(client,header.name,header.value.c_str());
    bool sent=esp_http_client_open(client,body.size())==ESP_OK&&sendAll(client,body.data(),body.size());if(!sent||esp_http_client_fetch_headers(client)<0){esp_http_client_cleanup(client);error="TTS network error";return false;}int status=esp_http_client_get_status_code(client);if(status<200||status>=300){esp_http_client_cleanup(client);error="TTS HTTP "+std::to_string(status);return false;}

    constexpr size_t INPUT_SIZE=4096,OUTPUT_SIZE=3072,OUTPUT_COUNT=3;std::unique_ptr<uint8_t,ByteDeleter> buffers(static_cast<uint8_t*>(heap_caps_malloc(INPUT_SIZE+OUTPUT_SIZE*OUTPUT_COUNT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));if(!buffers){esp_http_client_cleanup(client);error="TTS memory error";return false;}auto* input=buffers.get();auto* output=input+INPUT_SIZE;size_t output_index=0;
    M5.Mic.end();auto speaker_config=M5.Speaker.config();speaker_config.magnification=4;M5.Speaker.config(speaker_config);static constexpr uint8_t volumes[]{64,128,192,255};M5.Speaker.setVolume(volumes[std::min<uint8_t>(pending_settings_.volume_level,3)]);if(ampAllowed()&&!M5.Speaker.begin()){esp_http_client_cleanup(client);error="TTS playback error";return false;}M5.Speaker.setVolume(volumes[std::min<uint8_t>(pending_settings_.volume_level,3)]);

    enum class ParseState:uint8_t{FIND_KEY,FIND_COLON,FIND_QUOTE,READ_DATA};ParseState state=ParseState::FIND_KEY;constexpr char key[]="\"data\"";size_t key_match=0,encoded=0;bool played=false,read_failed=false;uint32_t smoothed=0;
    auto playEncoded=[&]()->bool{if(!encoded)return true;uint8_t* pcm=output+output_index*OUTPUT_SIZE;size_t decoded=0;if(mbedtls_base64_decode(pcm,OUTPUT_SIZE,&decoded,input,encoded)!=0)return false;encoded=0;if(!decoded)return true;if(decoded&1)return false;uint64_t sum=0;size_t samples=decoded/2;for(size_t i=0;i<samples;++i){int16_t value=static_cast<int16_t>(pcm[i*2]|(pcm[i*2+1]<<8));sum+=value<0?-static_cast<int32_t>(value):value;}uint32_t amplitude=samples?sum/samples:0;smoothed=(smoothed*2+amplitude)/3;mouth_level_.store(smoothed>=2200?2:smoothed>=450?1:0);if(!played){setState(VoiceState::PLAYING,"Playing...");played=true;}M5.Speaker.playRaw(reinterpret_cast<int16_t*>(pcm),samples,24000,false,1,0);output_index=(output_index+1)%OUTPUT_COUNT;return true;};
    std::array<uint8_t,2048> network{};while(!cancel_requested_.load()){int received=esp_http_client_read(client,reinterpret_cast<char*>(network.data()),network.size());if(received<0){read_failed=true;break;}if(received==0)break;for(int i=0;i<received&&!cancel_requested_.load();++i){uint8_t value=network[static_cast<size_t>(i)];switch(state){case ParseState::FIND_KEY:if(value==static_cast<uint8_t>(key[key_match])){if(++key_match==sizeof(key)-1){state=ParseState::FIND_COLON;key_match=0;}}else key_match=value=='\"'?1:0;break;case ParseState::FIND_COLON:if(value==':')state=ParseState::FIND_QUOTE;break;case ParseState::FIND_QUOTE:if(value=='\"')state=ParseState::READ_DATA;break;case ParseState::READ_DATA:if(value=='\"'){if(!playEncoded()){read_failed=true;}state=ParseState::FIND_KEY;}else if((value>='A'&&value<='Z')||(value>='a'&&value<='z')||(value>='0'&&value<='9')||value=='+'||value=='/'||value=='='){input[encoded++]=value;if(encoded==INPUT_SIZE&&!playEncoded())read_failed=true;}break;}if(read_failed)break;}if(read_failed)break;}
    if(!read_failed&&encoded&&!playEncoded()){read_failed=true;}
    esp_http_client_close(client);esp_http_client_cleanup(client);bool cancelled=cancel_requested_.load();
    if(played&&!read_failed&&!cancelled){while(M5.Speaker.isPlaying()&&!cancel_requested_.load())vTaskDelay(pdMS_TO_TICKS(10));cancelled=cancel_requested_.load();}
    if(cancelled){M5.Speaker.stop();}
    mouth_level_.store(0);M5.Speaker.end();
    if(cancelled)return false;
    if(read_failed){error="TTS network error";return false;}
    if(!played){error="TTS empty audio";return false;}
    return true;
}

bool VoiceChat::playStream(HttpReader& reader,size_t bytes_remaining,bool bounded){
    auto read=[&](void* destination,size_t length){if(bounded&&length>bytes_remaining)return false;if(!reader.readExact(destination,length))return false;if(bounded)bytes_remaining-=length;return true;};
    auto skip=[&](size_t length){if(bounded&&length>bytes_remaining)return false;if(!reader.skip(length))return false;if(bounded)bytes_remaining-=length;return true;};
    uint8_t riff[12];if(!read(riff,sizeof(riff))||std::memcmp(riff,"RIFF",4)!=0||std::memcmp(riff+8,"WAVE",4)!=0)return false;
    uint32_t rate=0;uint16_t channels=0,bits=0;bool started=false,success=false;constexpr size_t BUFFER_SIZE=4096,BUFFER_COUNT=3;std::unique_ptr<uint8_t,ByteDeleter> buffers(static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE*BUFFER_COUNT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));if(!buffers)return false;size_t buffer_index=0;
    while(!bounded||bytes_remaining>=8){
        uint8_t chunk_header[8];if(!read(chunk_header,sizeof(chunk_header)))break;uint32_t chunk_size=get32(chunk_header+4);if(bounded&&chunk_size>bytes_remaining)return false;
        if(std::memcmp(chunk_header,"fmt ",4)==0){uint8_t fmt[16];if(chunk_size<sizeof(fmt)||!read(fmt,sizeof(fmt)))return false;rate=get32(fmt+4);channels=get16(fmt+2);bits=get16(fmt+14);if(get16(fmt)!=1||(channels!=1&&channels!=2)||bits!=16||!rate||!skip(chunk_size-sizeof(fmt)))return false;}
        else if(std::memcmp(chunk_header,"data",4)==0){
            if(!rate||!channels)return false;
            M5.Mic.end();auto speaker_config=M5.Speaker.config();speaker_config.magnification=4;M5.Speaker.config(speaker_config);static constexpr uint8_t volumes[]{64,128,192,255};M5.Speaker.setVolume(volumes[std::min<uint8_t>(pending_settings_.volume_level,3)]);if(ampAllowed()&&!M5.Speaker.begin())return false;M5.Speaker.setVolume(volumes[std::min<uint8_t>(pending_settings_.volume_level,3)]);setState(VoiceState::PLAYING,"Playing...");started=true;mouth_level_.store(0);uint32_t smoothed=0;size_t remaining=chunk_size;bool unknown_length=!bounded&&chunk_size==UINT32_MAX;
            while((unknown_length||remaining)&&!cancel_requested_.load()){
                size_t count=unknown_length?BUFFER_SIZE:std::min(remaining,BUFFER_SIZE);uint8_t* buffer=buffers.get()+buffer_index*BUFFER_SIZE;
                if(unknown_length){int received=reader.readSome(buffer,count);if(received<0){success=false;break;}if(received==0){success=true;break;}count=static_cast<size_t>(received);}else if(!read(buffer,count)){success=false;break;}
                uint64_t sum=0;size_t samples=count/2;for(size_t i=0;i<samples;++i){int16_t value=static_cast<int16_t>(buffer[i*2]|(buffer[i*2+1]<<8));sum+=value<0?-static_cast<int32_t>(value):value;}uint32_t amplitude=samples?sum/samples:0;smoothed=(smoothed*2+amplitude)/3;mouth_level_.store(smoothed>=2200?2:smoothed>=450?1:0);M5.Speaker.playRaw(reinterpret_cast<int16_t*>(buffer),samples,rate,channels==2,1,0);buffer_index=(buffer_index+1)%BUFFER_COUNT;if(!unknown_length)remaining-=count;
            }
            if(!unknown_length)success=remaining==0&&!cancel_requested_.load();else success=success&&!cancel_requested_.load();if(success&&!unknown_length&&(chunk_size&1))success=skip(1);break;
        }else if(!skip(chunk_size))return false;
        if(chunk_size&1&&!skip(1))return false;
    }
    if(started){if(success){while(M5.Speaker.isPlaying()&&!cancel_requested_.load())vTaskDelay(pdMS_TO_TICKS(10));if(cancel_requested_.load()){M5.Speaker.stop();success=false;}}else M5.Speaker.stop();mouth_level_.store(0);M5.Speaker.end();}return started&&success;
}

bool VoiceChat::synthesize(const Settings& cfg,const std::string& text,std::vector<uint8_t>& out,bool& raw,std::string& error){const auto& s=cfg.tts;if(s.url.empty()||s.model.empty()){error="TTS config error";return false;}auto* root=cJSON_CreateObject();if(s.provider!="gemini")cJSON_AddStringToObject(root,"model",s.model.c_str());
    if(s.provider=="gemini"){std::string prompt=ttsPrompt(s.instructions,text);auto* contents=cJSON_AddArrayToObject(root,"contents");auto* item=cJSON_CreateObject();cJSON_AddItemToArray(contents,item);auto* parts=cJSON_AddArrayToObject(item,"parts");auto* p=cJSON_CreateObject();cJSON_AddStringToObject(p,"text",prompt.c_str());cJSON_AddItemToArray(parts,p);auto* gen=cJSON_AddObjectToObject(root,"generationConfig");auto* mods=cJSON_AddArrayToObject(gen,"responseModalities");cJSON_AddItemToArray(mods,cJSON_CreateString("AUDIO"));auto* speech=cJSON_AddObjectToObject(gen,"speechConfig");auto* voice=cJSON_AddObjectToObject(speech,"voiceConfig");auto* pre=cJSON_AddObjectToObject(voice,"prebuiltVoiceConfig");cJSON_AddStringToObject(pre,"voiceName",s.voice.empty()?"Kore":s.voice.c_str());}
    else{cJSON_AddStringToObject(root,"input",text.c_str());cJSON_AddStringToObject(root,"voice",s.voice.empty()?"alloy":s.voice.c_str());if(!s.instructions.empty())cJSON_AddStringToObject(root,"instructions",s.instructions.c_str());cJSON_AddStringToObject(root,"response_format","wav");}
    auto body=jsonPrint(root);cJSON_Delete(root);auto target=s.provider=="gemini"?geminiUrl(s.url,s.model):s.url;auto r=postJson(target,body,auth(s),MAX_GEMINI_AUDIO_RESPONSE);if(r.status<200||r.status>=300){error=r.status?"TTS HTTP "+std::to_string(r.status):"TTS network error";return false;}if(s.provider!="gemini"){out.swap(r.body);raw=false;if(out.empty()){error="TTS empty audio";return false;}return true;}auto* j=parse(r);if(!j){error="TTS response error";return false;}std::string data;auto* candidates=cJSON_GetObjectItem(j,"candidates");cJSON* candidate=nullptr;cJSON_ArrayForEach(candidate,candidates){auto* content=cJSON_GetObjectItem(candidate,"content");auto* parts=content?cJSON_GetObjectItem(content,"parts"):nullptr;cJSON* part=nullptr;cJSON_ArrayForEach(part,parts){auto* inlineData=cJSON_GetObjectItem(part,"inlineData");if(!inlineData)inlineData=cJSON_GetObjectItem(part,"inline_data");if(inlineData){data=jsonString(inlineData,"data");if(!data.empty())break;}}if(!data.empty())break;}cJSON_Delete(j);raw=true;if(data.empty()){error="TTS empty audio";return false;}if(!decode64(data,out)){error="TTS decode error";return false;}return true;}

bool VoiceChat::play(const std::vector<uint8_t>& audio,bool raw){if(audio.empty())return false;mouth_level_.store(0);M5.Mic.end();static constexpr uint8_t volumes[]{64,128,192,255};auto speaker_config=M5.Speaker.config();speaker_config.magnification=4;M5.Speaker.config(speaker_config);M5.Speaker.setVolume(volumes[std::min<uint8_t>(pending_settings_.volume_level,3)]);if(ampAllowed()&&!M5.Speaker.begin())return false;M5.Speaker.setVolume(volumes[std::min<uint8_t>(pending_settings_.volume_level,3)]);const uint8_t* data=audio.data();size_t size=audio.size();uint32_t rate=24000;uint16_t channels=1,bits=16;
    if(!raw&&size>=44&&std::memcmp(data,"RIFF",4)==0){rate=data[24]|data[25]<<8|data[26]<<16|data[27]<<24;channels=data[22]|data[23]<<8;bits=data[34]|data[35]<<8;size_t p=12;while(p+8<=size){uint32_t n=data[p+4]|data[p+5]<<8|data[p+6]<<16|data[p+7]<<24;if(std::memcmp(data+p,"data",4)==0){data+=p+8;size=std::min<size_t>(n,size-p-8);break;}p+=8+n+(n&1);}}
    if(bits!=16){M5.Speaker.end();return false;}constexpr size_t CHUNK=8192;uint32_t smoothed_amplitude=0;for(size_t p=0;p+1<size&&!cancel_requested_.load();p+=CHUNK){size_t n=std::min(CHUNK,size-p)&~size_t(1);auto* samples=reinterpret_cast<const int16_t*>(data+p);size_t count=n/2;uint64_t sum=0;for(size_t i=0;i<count;++i){int32_t v=samples[i];sum+=v<0?-v:v;}uint32_t amplitude=count?sum/count:0;smoothed_amplitude=(smoothed_amplitude*2+amplitude)/3;mouth_level_.store(smoothed_amplitude>=2200?2:smoothed_amplitude>=450?1:0);M5.Speaker.playRaw(samples,count,rate,channels==2,1,0);vTaskDelay(pdMS_TO_TICKS(80));}if(cancel_requested_.load())M5.Speaker.stop();else while(M5.Speaker.isPlaying()&&!cancel_requested_.load())vTaskDelay(pdMS_TO_TICKS(10));bool completed=!cancel_requested_.load();if(!completed)M5.Speaker.stop();mouth_level_.store(0);M5.Speaker.end();return completed;}
