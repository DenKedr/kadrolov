#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <filesystem>
using Microsoft::WRL::ComPtr;
static void u32(std::ofstream &f,uint32_t n){f.write(reinterpret_cast<char*>(&n),4);}
static int failed(const char* where,HRESULT hr){std::cerr<<where<<": HRESULT "<<std::hex<<hr<<std::endl;return 1;}
int wmain(int argc,wchar_t **argv){
 std::wstring output,mode=L"system";
 for(int i=1;i+1<argc;i+=2){std::wstring key=argv[i];if(key==L"--output")output=argv[i+1];else if(key==L"--mode")mode=argv[i+1];else return 2;}
 if(output.empty()||(mode!=L"system"&&mode!=L"mic"))return 2;
 HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(hr))return failed("COM",hr);
 ComPtr<IMMDeviceEnumerator> devices;hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices));if(FAILED(hr))return failed("Devices",hr);
 ComPtr<IMMDevice> device;hr=devices->GetDefaultAudioEndpoint(mode==L"system"?eRender:eCapture,eConsole,&device);if(FAILED(hr))return failed("Audio endpoint",hr);
 ComPtr<IAudioClient> client;hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf()));if(FAILED(hr))return failed("Audio client",hr);
 WAVEFORMATEX *format=nullptr;hr=client->GetMixFormat(&format);if(FAILED(hr))return failed("Audio format",hr);
 const UINT32 formatBytes=sizeof(WAVEFORMATEX)+format->cbSize;
 hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,mode==L"system"?AUDCLNT_STREAMFLAGS_LOOPBACK:0,10000000,0,format,nullptr);if(FAILED(hr)){CoTaskMemFree(format);return failed("Initialize",hr);}
 ComPtr<IAudioCaptureClient> capture;hr=client->GetService(IID_PPV_ARGS(&capture));if(FAILED(hr)){CoTaskMemFree(format);return failed("Capture",hr);}
 std::ofstream f(std::filesystem::path(output),std::ios::binary);if(!f){CoTaskMemFree(format);return 3;}
 f.write("RIFF",4);u32(f,0);f.write("WAVEfmt ",8);u32(f,formatBytes);f.write(reinterpret_cast<char*>(format),formatBytes);
 f.write("data",4);const auto sizeOffset=f.tellp();u32(f,0);uint64_t dataBytes=0;
 std::atomic<bool> stop{false};
 std::thread commands([&]{std::string line;while(std::getline(std::cin,line)){if(line.find("stop")!=std::string::npos)break;}stop=true;});
 commands.detach();
 hr=client->Start();if(FAILED(hr)){CoTaskMemFree(format);return failed("Start",hr);}
 LARGE_INTEGER frequency{},origin{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&origin);
 auto now100ns=[&](){LARGE_INTEGER n{};QueryPerformanceCounter(&n);return static_cast<uint64_t>((static_cast<long double>(n.QuadPart)*10000000.0L)/frequency.QuadPart);};
 const uint64_t origin100ns=now100ns();
 auto padTo=[&](uint64_t frames){const uint64_t written=dataBytes/format->nBlockAlign;if(frames<=written)return;uint64_t bytes=(frames-written)*format->nBlockAlign;if(dataBytes+bytes>0xffff0000ULL){stop=true;return;}std::vector<char> zero(65536,0);while(bytes&&f){auto n=static_cast<std::streamsize>(std::min<uint64_t>(bytes,zero.size()));f.write(zero.data(),n);bytes-=n;dataBytes+=n;}};
 std::cout<<"{\"event\":\"started\"}"<<std::endl;
 bool ok=true;
 while(!stop){
  UINT32 packets=0;hr=capture->GetNextPacketSize(&packets);if(FAILED(hr)){ok=false;break;}
  while(packets){
   BYTE *data=nullptr;UINT32 frames=0;DWORD flags=0;
   UINT64 position=0,timestamp=0;hr=capture->GetBuffer(&data,&frames,&flags,&position,&timestamp);if(FAILED(hr)){ok=false;break;}
   const uint64_t frameAt=timestamp>origin100ns?(timestamp-origin100ns)*format->nSamplesPerSec/10000000ULL:0;
   padTo(frameAt);
   const uint64_t writtenFrames=dataBytes/format->nBlockAlign;
   const uint64_t overlap=writtenFrames>frameAt?std::min<uint64_t>(writtenFrames-frameAt,frames):0;
   const auto count=static_cast<uint32_t>((frames-overlap)*format->nBlockAlign);
   if(dataBytes+count>0xffff0000ULL){capture->ReleaseBuffer(frames);ok=false;std::cerr<<"WAV segment size limit"<<std::endl;break;}
   if(flags&AUDCLNT_BUFFERFLAGS_SILENT){std::vector<char> silence(count,0);f.write(silence.data(),count);}else f.write(reinterpret_cast<char*>(data)+overlap*format->nBlockAlign,count);
   dataBytes+=count;capture->ReleaseBuffer(frames);if(!f){ok=false;break;}
   hr=capture->GetNextPacketSize(&packets);if(FAILED(hr)){ok=false;break;}
  }
  if(!ok)break;
  // Loopback may emit no packets during silence. Keep 50 ms for device latency.
  const uint64_t elapsedFrames=(now100ns()-origin100ns)*format->nSamplesPerSec/10000000ULL;
  if(elapsedFrames>format->nSamplesPerSec/20)padTo(elapsedFrames-format->nSamplesPerSec/20);
  Sleep(5);
 }
 client->Stop();padTo((now100ns()-origin100ns)*format->nSamplesPerSec/10000000ULL);
 const auto end=f.tellp();f.seekp(sizeOffset);u32(f,static_cast<uint32_t>(dataBytes));f.seekp(4);u32(f,static_cast<uint32_t>(static_cast<std::streamoff>(end)-8));f.close();CoTaskMemFree(format);
 std::cout<<"{\"event\":\"finished\"}"<<std::endl;return ok?0:4;
}
