// Windows 10+, C++17. No third-party runtime or codec executable.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <climits>
#include <cstddef>
#include <memory>
#include <cstring>
#include <cwchar>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
static std::atomic<int> request{0}; // 1 pause, 2 resume, 3 stop
static void check(HRESULT hr,const char* what){if(FAILED(hr)){std::ostringstream s;s<<what<<" (HRESULT 0x"<<std::hex<<static_cast<unsigned long>(hr)<<")";throw std::runtime_error(s.str());}}
static std::string utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
static std::string quote(const std::string&s){std::string r="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){r+='\\';r+=c;}else if(c<32){const char*h="0123456789abcdef";r+="\\u00";r+=h[c>>4];r+=h[c&15];}else r+=c;}return r+'"';}
static void event(const char* e,const std::wstring& path,int tracks,const std::string& message=""){std::cout<<"{\"event\":"<<quote(e)<<",\"path\":"<<quote(utf8(path))<<",\"audioTracks\":"<<tracks<<",\"message\":"<<quote(message)<<"}"<<std::endl;}
static LONGLONG now(){LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);return (q.QuadPart/f.QuadPart)*10000000+(q.QuadPart%f.QuadPart)*10000000/f.QuadPart;}
struct Options{std::wstring output,audio=L"none",cameraDevice,microphone;int x=0,y=0,w=0,h=0,width=0,height=0,fps=30,quality=23;bool cursor=true,clicks=false,camera=false;};
static int number(const std::wstring&s){size_t end=0;long n=std::stol(s,&end);if(end!=s.size()||n<INT_MIN||n>INT_MAX)throw std::runtime_error("Invalid integer");return int(n);}
static Options options(int argc,wchar_t**argv){Options o;for(int i=1;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Every option requires a value");std::wstring k=argv[i],v=argv[i+1];if(k==L"--output")o.output=v;else if(k==L"--audio")o.audio=v;else if(k==L"--camera-device")o.cameraDevice=v;else if(k==L"--microphone")o.microphone=v;else if(k==L"--rect"){std::wistringstream s(v);wchar_t a,b,c;if(!(s>>o.x>>a>>o.y>>b>>o.w>>c>>o.h)||a!=L','||b!=L','||c!=L','||!s.eof())throw std::runtime_error("Invalid rectangle");}else if(k==L"--fps")o.fps=number(v);else if(k==L"--width")o.width=number(v);else if(k==L"--height")o.height=number(v);else if(k==L"--quality")o.quality=number(v);else if(k==L"--cursor"||k==L"--clicks"||k==L"--camera"){if(v!=L"0"&&v!=L"1")throw std::runtime_error("Boolean must be 0 or 1");bool b=v==L"1";if(k==L"--cursor")o.cursor=b;else if(k==L"--clicks")o.clicks=b;else o.camera=b;}else throw std::runtime_error("Unknown option");}
 if(o.output.empty()||o.w<1||o.h<1||o.w>32768||o.h>32768||o.fps<1||o.fps>60||o.quality<0||o.quality>51)throw std::runtime_error("Invalid output, rectangle, FPS or quality");if(!o.width)o.width=o.w&~1;if(!o.height)o.height=o.h&~1;if(o.width<2||o.height<2||o.width>8192||o.height>8192||(o.width&1)||(o.height&1))throw std::runtime_error("Encoding dimensions must be even and between 2 and 8192");if(o.audio!=L"none"&&o.audio!=L"system"&&o.audio!=L"mic"&&o.audio!=L"both")throw std::runtime_error("Invalid audio mode");return o;}
// Only one asynchronous read is outstanding. Callback owns shared state, never Camera.
// The worker therefore never blocks inside a synchronous camera ReadSample on shutdown.
struct CameraState {
 std::mutex mutex;
 HANDLE wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 std::atomic<bool> stop{false};
 ComPtr<IMFSample> sample;
 HRESULT status=S_OK;
 DWORD flags=0;
 bool received=false,ready=false;
 UINT32 width=0,height=0;
 std::vector<BYTE> pixels;
 std::string error;
 LONGLONG updated=0;
 ~CameraState(){if(wake)CloseHandle(wake);}
};
class CameraCallback final:public IMFSourceReaderCallback {
 std::atomic<ULONG> references{1};
 std::shared_ptr<CameraState> state;
public:
 explicit CameraCallback(std::shared_ptr<CameraState> s):state(std::move(s)){}
 STDMETHODIMP QueryInterface(REFIID id,void**out) override {
  if(!out)return E_POINTER;*out=nullptr;
  if(id==__uuidof(IUnknown)||id==__uuidof(IMFSourceReaderCallback)){*out=static_cast<IMFSourceReaderCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
 }
 STDMETHODIMP_(ULONG) AddRef() override{return ++references;}
 STDMETHODIMP_(ULONG) Release() override{ULONG n=--references;if(!n)delete this;return n;}
 STDMETHODIMP OnReadSample(HRESULT hr,DWORD,DWORD flags,LONGLONG,IMFSample*sample) override {
  {std::lock_guard<std::mutex> lock(state->mutex);state->status=hr;state->flags=flags;state->sample=sample;state->received=true;}
  SetEvent(state->wake);return S_OK;
 }
 STDMETHODIMP OnEvent(DWORD,IMFMediaEvent*) override{return S_OK;}
 STDMETHODIMP OnFlush(DWORD) override{SetEvent(state->wake);return S_OK;}
};
struct Camera {
 std::shared_ptr<CameraState> state=std::make_shared<CameraState>();
 std::thread worker;
 static std::wstring attribute(IMFAttributes*a,REFGUID key){wchar_t*value=nullptr;UINT32 n=0;HRESULT hr=a->GetAllocatedString(key,&value,&n);if(FAILED(hr))return {};std::wstring result(value,n);CoTaskMemFree(value);return result;}
 explicit Camera(const std::wstring&device){
  if(!state->wake)throw std::runtime_error("Cannot create camera event");
  worker=std::thread([s=state,device]{run(s,device);});
  const LONGLONG deadline=now()+150000000;
  while(now()<deadline){
   std::string error;{std::lock_guard<std::mutex> lock(state->mutex);if(state->ready)return;error=state->error;}if(!error.empty()){state->stop=true;SetEvent(state->wake);worker.join();throw std::runtime_error(error);}
   Sleep(10);
  }
  state->stop=true;SetEvent(state->wake);worker.join();throw std::runtime_error("Camera did not deliver a frame within 15 seconds");
 }
 ~Camera(){state->stop=true;SetEvent(state->wake);if(worker.joinable())worker.join();}
 static void run(std::shared_ptr<CameraState>s,const std::wstring&selected){
  bool com=false;ComPtr<IMFMediaSource>source;ComPtr<IMFSourceReader>reader;
  try {
   check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Camera COM");com=true;
   ComPtr<IMFAttributes>a;check(MFCreateAttributes(&a,2),"Camera enumeration attributes");check(a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),"Camera source type");
   IMFActivate**devices=nullptr;UINT32 count=0;check(MFEnumDeviceSources(a.Get(),&devices,&count),"Enumerate cameras");ComPtr<IMFActivate>chosen;
   for(UINT32 i=0;i<count;++i){if(!chosen&&(selected.empty()||selected==attribute(devices[i],MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)||selected==attribute(devices[i],MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME)))chosen=devices[i];devices[i]->Release();}CoTaskMemFree(devices);
   if(!chosen)throw std::runtime_error(selected.empty()?"No camera is available":"Selected camera was not found");
   check(chosen->ActivateObject(IID_PPV_ARGS(&source)),"Open camera (check Windows camera privacy settings)");
   check(MFCreateAttributes(&a,3),"Camera reader attributes");ComPtr<CameraCallback>callback;callback.Attach(new CameraCallback(s));check(a->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,callback.Get()),"Camera callback");check(a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE),"Camera RGB conversion");check(MFCreateSourceReaderFromMediaSource(source.Get(),a.Get(),&reader),"Create camera reader");
   check(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),"Disable camera streams");check(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE),"Enable camera video");
   ComPtr<IMFMediaType>type;check(MFCreateMediaType(&type),"Camera media type");check(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"Camera major type");check(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32),"Camera RGB32 type");check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type.Get()),"Configure camera RGB32");
   while(!s->stop){
    check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,nullptr,nullptr,nullptr),"Request camera frame");
    ComPtr<IMFSample>sample;HRESULT status=S_OK;DWORD flags=0;
    while(!s->stop){WaitForSingleObject(s->wake,100);std::lock_guard<std::mutex> lock(s->mutex);if(s->received){sample=std::move(s->sample);status=s->status;flags=s->flags;s->received=false;break;}}
    if(s->stop)break;check(status,"Camera capture (device disconnected or access denied)");if(flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_ENDOFSTREAM))throw std::runtime_error("Camera stream ended");if(!sample)continue;
    check(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&type),"Camera frame format");UINT32 w=0,h=0;check(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h),"Camera dimensions");if(!w||!h||w>8192||h>8192)throw std::runtime_error("Unsupported camera dimensions");
    ComPtr<IMFMediaBuffer>buffer;check(sample->ConvertToContiguousBuffer(&buffer),"Camera frame buffer");ComPtr<IMF2DBuffer>twoD;BYTE*scanline=nullptr;LONG stride=LONG(w*4);bool locked2D=false;DWORD bytes=0;
    if(SUCCEEDED(buffer.As(&twoD))&&SUCCEEDED(twoD->Lock2D(&scanline,&stride)))locked2D=true;
    else {check(buffer->Lock(&scanline,nullptr,&bytes),"Lock camera frame");UINT32 rawStride=0;if(SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&rawStride)))stride=LONG(rawStride);if(uint64_t(stride<0?-int64_t(stride):stride)*h>bytes||int64_t(stride<0?-int64_t(stride):stride)<int64_t(w)*4){buffer->Unlock();throw std::runtime_error("Invalid camera RGB buffer stride");}if(stride<0)scanline+=size_t(h-1)*size_t(-int64_t(stride));}
    std::vector<BYTE>pixels(size_t(w)*h*4);for(UINT32 y=0;y<h;++y)std::memcpy(pixels.data()+size_t(y)*w*4,scanline+ptrdiff_t(y)*stride,size_t(w)*4);
    if(locked2D)check(twoD->Unlock2D(),"Unlock camera image");else check(buffer->Unlock(),"Unlock camera image");
    {std::lock_guard<std::mutex> lock(s->mutex);s->width=w;s->height=h;s->pixels=std::move(pixels);s->updated=now();s->ready=true;}
   }
  }catch(const std::exception&e){std::lock_guard<std::mutex>lock(s->mutex);s->error=e.what();}
  // Async ReadSample returns immediately; shutdown cancels the outstanding request.
  if(reader)reader->Flush(MF_SOURCE_READER_ALL_STREAMS);
  if(source)source->Shutdown();reader.Reset();source.Reset();if(com)CoUninitialize();
 }
 void overlay(HDC dc,int width,int height){
  std::lock_guard<std::mutex>lock(state->mutex);if(!state->error.empty())throw std::runtime_error(state->error);if(now()-state->updated>50000000)throw std::runtime_error("Camera stopped delivering frames");
  int w=std::max(1,width/4),h=int(int64_t(w)*state->height/state->width);if(h>height/3){h=std::max(1,height/3);w=std::max(1,int(int64_t(h)*state->width/state->height));}int margin=std::min(16,std::min(width,height)/32);
  BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=LONG(state->width);bi.bmiHeader.biHeight=-LONG(state->height);bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
  int drawn=StretchDIBits(dc,width-w-margin,height-h-margin,w,h,0,0,int(state->width),int(state->height),state->pixels.data(),&bi,DIB_RGB_COLORS,SRCCOPY);if(drawn==0||drawn==GDI_ERROR)throw std::runtime_error("Composite camera frame failed");GdiFlush();
 }
};

struct Frame{
 HDC screen=nullptr,dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;void* pixels=nullptr;int w,h;
 Frame(int width,int height):w(width),h(height){screen=GetDC(nullptr);dc=CreateCompatibleDC(screen);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;bitmap=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&pixels,nullptr,0);if(!screen||!dc||!bitmap)throw std::runtime_error("Cannot create screen capture surface");previous=SelectObject(dc,bitmap);SetStretchBltMode(dc,HALFTONE);}
 ~Frame(){if(previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(screen)ReleaseDC(nullptr,screen);}
 void capture(const Options&o){if(!StretchBlt(dc,0,0,w,h,screen,o.x,o.y,o.w,o.h,SRCCOPY|CAPTUREBLT))throw std::runtime_error("Screen capture failed");if(o.cursor){CURSORINFO ci{sizeof(ci)};if(GetCursorInfo(&ci)&&(ci.flags&CURSOR_SHOWING)){ICONINFO ii{};if(GetIconInfo(ci.hCursor,&ii)){int x=int((int64_t(ci.ptScreenPos.x)-o.x-ii.xHotspot)*w/o.w),y=int((int64_t(ci.ptScreenPos.y)-o.y-ii.yHotspot)*h/o.h);DrawIconEx(dc,x,y,ci.hCursor,0,0,0,nullptr,DI_NORMAL);if(ii.hbmMask)DeleteObject(ii.hbmMask);if(ii.hbmColor)DeleteObject(ii.hbmColor);}}}if(o.clicks&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){POINT p;GetCursorPos(&p);int x=int((int64_t(p.x)-o.x)*w/o.w),y=int((int64_t(p.y)-o.y)*h/o.h);HPEN pen=CreatePen(PS_SOLID,3,RGB(255,100,40));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Ellipse(dc,x-15,y-15,x+15,y+15);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);}GdiFlush();}
};
static void sample(IMFSinkWriter*w,DWORD stream,const void*data,DWORD bytes,LONGLONG time,LONGLONG duration){ComPtr<IMFMediaBuffer>b;ComPtr<IMFSample>s;check(MFCreateMemoryBuffer(bytes,&b),"Create media buffer");BYTE*p=nullptr;check(b->Lock(&p,nullptr,nullptr),"Lock media buffer");std::memcpy(p,data,bytes);check(b->Unlock(),"Unlock media buffer");check(b->SetCurrentLength(bytes),"Set buffer length");check(MFCreateSample(&s),"Create sample");check(s->AddBuffer(b.Get()),"Add buffer");check(s->SetSampleTime(time),"Set sample time");check(s->SetSampleDuration(duration),"Set sample duration");check(w->WriteSample(stream,s.Get()),"Encode sample");}
static ComPtr<IMFMediaType> media(const GUID& major,const GUID& subtype){ComPtr<IMFMediaType>t;check(MFCreateMediaType(&t),"Create media type");check(t->SetGUID(MF_MT_MAJOR_TYPE,major),"Major type");check(t->SetGUID(MF_MT_SUBTYPE,subtype),"Subtype");return t;}
static void videoType(IMFMediaType*t,const Options&o){check(MFSetAttributeSize(t,MF_MT_FRAME_SIZE,o.width,o.height),"Frame size");check(MFSetAttributeRatio(t,MF_MT_FRAME_RATE,o.fps,1),"Frame rate");check(MFSetAttributeRatio(t,MF_MT_PIXEL_ASPECT_RATIO,1,1),"Pixel aspect");check(t->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive),"Interlace");}
struct Block{int64_t first;std::vector<int16_t>pcm;};
struct Audio{
 ComPtr<IAudioClient>client;ComPtr<IAudioCaptureClient>capture;std::deque<Block>blocks;bool started=false;
 explicit Audio(bool loopback,const std::wstring&chosen=L""){ComPtr<IMMDeviceEnumerator>e;ComPtr<IMMDevice>d;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)),"Audio enumerator");if(!loopback&&!chosen.empty()){check(e->GetDevice(chosen.c_str(),&d),"Selected microphone was not found");DWORD state=0;check(d->GetState(&state),"Selected microphone state");if(!(state&DEVICE_STATE_ACTIVE))throw std::runtime_error("Selected microphone is disconnected or disabled");ComPtr<IMMEndpoint>endpoint;check(d.As(&endpoint),"Selected microphone endpoint");EDataFlow flow;check(endpoint->GetDataFlow(&flow),"Selected microphone direction");if(flow!=eCapture)throw std::runtime_error("Selected device is not a microphone");}else check(e->GetDefaultAudioEndpoint(loopback?eRender:eCapture,eConsole,&d),"Audio endpoint");check(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"Activate audio");WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=48000;f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=192000;DWORD flags=AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;if(loopback)flags|=AUDCLNT_STREAMFLAGS_LOOPBACK;check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,2000000,0,&f,nullptr),"Initialize 48kHz stereo audio");check(client->GetService(IID_PPV_ARGS(&capture)),"Audio capture service");}
 void start(){check(client->Start(),"Start audio");started=true;}
 ~Audio(){if(started)client->Stop();}
 void drain(LONGLONG origin,bool discard){UINT32 n=0;check(capture->GetNextPacketSize(&n),"Audio packet size");while(n){BYTE*p;UINT32 frames;DWORD flags;UINT64 position,qpc;check(capture->GetBuffer(&p,&frames,&flags,&position,&qpc),"Read audio packet");if(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)qpc=UINT64(now()-LONGLONG(frames)*10000000/48000);Block b;b.first=((LONGLONG(qpc)-origin)*48000)/10000000;b.pcm.resize(size_t(frames)*2);if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT))std::memcpy(b.pcm.data(),p,b.pcm.size()*2);HRESULT released=capture->ReleaseBuffer(frames);check(released,"Release audio packet");if(!discard){blocks.push_back(std::move(b));if(blocks.size()>1024)throw std::runtime_error("Audio capture backlog exceeded");}check(capture->GetNextPacketSize(&n),"Audio packet size");}}
 void mix(int64_t first,int frames,std::vector<int32_t>&out){int64_t end=first+frames;while(!blocks.empty()&&blocks.front().first+int64_t(blocks.front().pcm.size()/2)<=first)blocks.pop_front();for(const auto&b:blocks){int64_t begin=std::max(first,b.first),last=std::min(end,b.first+int64_t(b.pcm.size()/2));for(int64_t f=begin;f<last;++f)for(int c=0;c<2;++c)out[size_t(f-first)*2+c]+=b.pcm[size_t(f-b.first)*2+c];}}
 void clear(){blocks.clear();}
};
static void commands(){std::string line;while(std::getline(std::cin,line)){std::string compact;for(char c:line)if(c!=' '&&c!='\t'&&c!='\r')compact+=c;if(compact=="{\"command\":\"pause\"}")request=1;else if(compact=="{\"command\":\"resume\"}")request=2;else if(compact=="{\"command\":\"stop\"}"){request=3;return;}}request=3;}
static int listMicrophones(){
 bool com=false;try{check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Initialize microphone enumeration");com=true;std::string result;{
 ComPtr<IMMDeviceEnumerator>enumerator;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)),"Microphone enumerator");ComPtr<IMMDeviceCollection>devices;check(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&devices),"Enumerate microphones");UINT count=0;check(devices->GetCount(&count),"Microphone count");result="{\"microphones\":[";
 for(UINT i=0;i<count;++i){ComPtr<IMMDevice>device;check(devices->Item(i,&device),"Microphone item");LPWSTR raw=nullptr;check(device->GetId(&raw),"Microphone ID");std::wstring id(raw);CoTaskMemFree(raw);ComPtr<IPropertyStore>properties;check(device->OpenPropertyStore(STGM_READ,&properties),"Microphone properties");PROPVARIANT value{};check(properties->GetValue(PKEY_Device_FriendlyName,&value),"Microphone friendly name");std::wstring name=value.vt==VT_LPWSTR&&value.pwszVal?value.pwszVal:id;PropVariantClear(&value);if(i)result+=",";result+="{\"id\":"+quote(utf8(id))+",\"name\":"+quote(utf8(name))+"}";}
 result+="]}";}std::cout<<result<<std::endl;CoUninitialize();return 0;
 }catch(const std::exception&e){if(com)CoUninitialize();event("error",L"",0,e.what());return 1;}
}
int wmain(int argc,wchar_t**argv){if(argc==2&&std::wstring(argv[1])==L"--list-microphones")return listMicrophones();Options o;bool com=false,mf=false;int tracks=0;try{o=options(argc,argv);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Initialize COM");com=true;check(MFStartup(MF_VERSION),"Initialize Media Foundation");mf=true;
 {
 Frame frame(o.width,o.height);std::unique_ptr<Camera>camera;if(o.camera)camera=std::make_unique<Camera>(o.cameraDevice);std::vector<std::unique_ptr<Audio>>audio;if(o.audio==L"system"||o.audio==L"both")audio.emplace_back(new Audio(true));if(o.audio==L"mic"||o.audio==L"both")audio.emplace_back(new Audio(false,o.microphone));tracks=audio.empty()?0:1;
 ComPtr<IMFAttributes>attrs;check(MFCreateAttributes(&attrs,2),"Writer attributes");check(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE),"Hardware transforms");check(attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE),"Writer throttling");ComPtr<IMFSinkWriter>writer;check(MFCreateSinkWriterFromURL(o.output.c_str(),nullptr,attrs.Get(),&writer),"Create MP4 writer");DWORD video=0,sound=0;auto v=media(MFMediaType_Video,MFVideoFormat_H264);videoType(v.Get(),o);const uint64_t rate=uint64_t(o.width)*o.height*o.fps;const double factor=0.03+0.27*(51-o.quality)/51.0;check(v->SetUINT32(MF_MT_AVG_BITRATE,UINT32(std::clamp<double>(rate*factor,200000,50000000))),"Video bitrate");check(writer->AddStream(v.Get(),&video),"Add H264 stream");v=media(MFMediaType_Video,MFVideoFormat_RGB32);videoType(v.Get(),o);check(v->SetUINT32(MF_MT_DEFAULT_STRIDE,UINT32(o.width*4)),"Video stride");check(writer->SetInputMediaType(video,v.Get(),nullptr),"Configure RGB32 input");
 if(tracks){auto a=media(MFMediaType_Audio,MFAudioFormat_AAC);check(a->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2),"AAC channels");check(a->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000),"AAC sample rate");check(a->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16),"AAC bits");check(a->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,24000),"AAC bitrate");check(a->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE,0),"AAC payload");check(a->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION,0x29),"AAC profile");check(writer->AddStream(a.Get(),&sound),"Add AAC stream");a=media(MFMediaType_Audio,MFAudioFormat_PCM);check(a->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2),"PCM channels");check(a->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000),"PCM rate");check(a->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16),"PCM bits");check(a->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4),"PCM alignment");check(a->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,192000),"PCM bytes per second");check(writer->SetInputMediaType(sound,a.Get(),nullptr),"Configure PCM input");}
 check(writer->BeginWriting(),"Begin MP4");LONGLONG origin=now(),pauseAt=0;bool paused=false;uint64_t frameIndex=0;int64_t audioIndex=0;for(auto&a:audio)a->start();std::thread(commands).detach();event("started",o.output,tracks);
 auto writeAudio=[&](int64_t until){while(audioIndex<until){int count=int(std::min<int64_t>(480,until-audioIndex));std::vector<int32_t>mixed(size_t(count)*2,0);for(auto&a:audio)a->mix(audioIndex,count,mixed);std::vector<int16_t>pcm(mixed.size());for(size_t i=0;i<pcm.size();++i)pcm[i]=int16_t(std::clamp(mixed[i]/int(audio.size()),-32768,32767));sample(writer.Get(),sound,pcm.data(),DWORD(pcm.size()*2),audioIndex*10000000/48000,count*10000000LL/48000);audioIndex+=count;}};
 while(true){int command=request.exchange(0);LONGLONG t=now();if(command==3)break;if(command==1&&!paused){paused=true;pauseAt=t;event("paused",o.output,tracks);}if(command==2&&paused){for(auto&a:audio)a->drain(origin,true);origin+=t-pauseAt;paused=false;event("resumed",o.output,tracks);}for(auto&a:audio)a->drain(origin,paused);if(paused){Sleep(5);continue;}LONGLONG elapsed=t-origin;LONGLONG next=frameIndex*10000000/o.fps;if(elapsed>=next){frameIndex=std::max<uint64_t>(frameIndex,uint64_t(elapsed)*o.fps/10000000);next=frameIndex*10000000/o.fps;frame.capture(o);if(camera)camera->overlay(frame.dc,o.width,o.height);sample(writer.Get(),video,frame.pixels,DWORD(o.width*o.height*4),next,(frameIndex+1)*10000000/o.fps-next);++frameIndex;}if(tracks)writeAudio(std::min<int64_t>(int64_t(frameIndex)*48000/o.fps,std::max<int64_t>(0,(elapsed-1000000)*48000/10000000)));Sleep(1);}
 for(auto&a:audio)a->drain(origin,paused);if(frameIndex==0){frame.capture(o);if(camera)camera->overlay(frame.dc,o.width,o.height);sample(writer.Get(),video,frame.pixels,DWORD(o.width*o.height*4),0,10000000/o.fps);frameIndex=1;}if(tracks)writeAudio(int64_t(frameIndex)*48000/o.fps);check(writer->Finalize(),"Finalize MP4");
 }
 MFShutdown();CoUninitialize();event("finished",o.output,tracks);return 0;
 }catch(const std::exception&e){if(mf)MFShutdown();if(com)CoUninitialize();event("error",o.output,tracks,e.what());return 1;}}
