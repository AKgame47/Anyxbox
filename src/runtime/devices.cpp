#include "runtime/devices.h"
#include <cstring>
#if defined(__linux__) && defined(__x86_64__)
#define MSABI __attribute__((ms_abi))
namespace anyxbox {
namespace {
XInputContext* XI = nullptr;
XAudio2Context* XA = nullptr;
const uint32_t ERROR_DEVICE_NOT_CONNECTED = 1167, ERROR_EMPTY = 4306, ERROR_BAD_ARGUMENTS = 160;
// ---- XInput ----
MSABI uint32_t x_GetState(uint32_t i, uint8_t* st) {
  if (i >= 4 || !st) return i >= 4 ? ERROR_DEVICE_NOT_CONNECTED : ERROR_BAD_ARGUMENTS;
  std::lock_guard<std::mutex> l(XI->m);
  const XInputPad& p = XI->pads[i];
  if (!p.connected) return ERROR_DEVICE_NOT_CONNECTED;
  std::memset(st, 0, 16);
  std::memcpy(st, &p.packet, 4); std::memcpy(st + 4, &p.buttons, 2); st[6] = p.lt; st[7] = p.rt;
  std::memcpy(st + 8, &p.lx, 2); std::memcpy(st + 10, &p.ly, 2); std::memcpy(st + 12, &p.rx, 2); std::memcpy(st + 14, &p.ry, 2);
  return 0;
}
MSABI uint32_t x_SetState(uint32_t i, const uint8_t* v) {
  if (i >= 4) return ERROR_DEVICE_NOT_CONNECTED;
  if (!v) return ERROR_BAD_ARGUMENTS;
  std::lock_guard<std::mutex> l(XI->m);
  if (!XI->pads[i].connected) return ERROR_DEVICE_NOT_CONNECTED;
  std::memcpy(&XI->pads[i].rumble_left, v, 2); std::memcpy(&XI->pads[i].rumble_right, v + 2, 2);
  return 0;
}
MSABI uint32_t x_GetCapabilities(uint32_t i, uint32_t, uint8_t* caps) {
  if (i >= 4) return ERROR_DEVICE_NOT_CONNECTED;
  if (!caps) return ERROR_BAD_ARGUMENTS;
  std::lock_guard<std::mutex> l(XI->m);
  if (!XI->pads[i].connected) return ERROR_DEVICE_NOT_CONNECTED;
  std::memset(caps, 0, 20);
  caps[0] = 1; caps[1] = 1;                                   // XINPUT_DEVTYPE_GAMEPAD, XINPUT_DEVSUBTYPE_GAMEPAD
  uint16_t all = 0xF3FF; std::memcpy(caps + 4, &all, 2);       // every standard button bit
  caps[6] = 255; caps[7] = 255;
  int16_t axis = -1; for (int k = 0; k < 4; k++) std::memcpy(caps + 8 + 2 * k, &axis, 2);
  uint16_t motor = 0xFFFF; std::memcpy(caps + 16, &motor, 2); std::memcpy(caps + 18, &motor, 2);
  return 0;
}
MSABI void x_Enable(int32_t on) { std::lock_guard<std::mutex> l(XI->m); XI->enabled = on != 0; }
MSABI uint32_t x_GetBattery(uint32_t i, uint8_t, uint8_t* info) {
  if (i >= 4) return ERROR_DEVICE_NOT_CONNECTED;
  if (!info) return ERROR_BAD_ARGUMENTS;
  std::lock_guard<std::mutex> l(XI->m);
  if (!XI->pads[i].connected) return ERROR_DEVICE_NOT_CONNECTED;
  info[0] = 1; info[1] = 3;                                   // BATTERY_TYPE_WIRED, BATTERY_LEVEL_FULL
  return 0;
}
MSABI uint32_t x_GetKeystroke(uint32_t i, uint32_t, uint8_t*) {
  if (i != 0xFF && i >= 4) return ERROR_DEVICE_NOT_CONNECTED;
  return ERROR_EMPTY;                                         // no keystroke events are generated
}
// ---- XAudio2 ----
enum VoiceKind { Source, Submix, Mastering };
}  // namespace
struct XAudioVoice {
  const void* const* vtbl;       // first member: what the guest sees as the COM object
  VoiceKind kind = Source;
  uint32_t block_align = 4, rate = 44100, channels = 2;
  float volume = 1.0f, freq_ratio = 1.0f;
  uint64_t samples_played = 0;
  bool destroyed = false, started = false;
};
namespace {
struct Engine { const void* const* vtbl; uint32_t refs = 1; };
const void* const* voice_vtbl();
const void* const* engine_vtbl();
XAudioVoice* new_voice(VoiceKind k) {
  XAudioVoice* v = new XAudioVoice(); v->vtbl = voice_vtbl(); v->kind = k;
  std::lock_guard<std::mutex> l(XA->m);
  XA->voices.push_back(v);
  return v;
}
MSABI uint32_t e_QueryInterface(Engine* e, const void*, void** out) { if (!out) return 0x80004003u; *out = e; e->refs++; return 0; }
MSABI uint32_t e_AddRef(Engine* e) { return ++e->refs; }
MSABI uint32_t e_Release(Engine* e) { return e->refs ? --e->refs : 0; }
MSABI uint32_t e_Ok() { return 0; }
MSABI void e_Void() {}
MSABI uint32_t e_CreateSourceVoice(Engine*, XAudioVoice** out, const uint8_t* fmt, uint32_t, float, void*, void*, void*) {
  if (!out || !fmt) return 0x80070057u;   // E_INVALIDARG
  XAudioVoice* v = new_voice(Source);
  uint16_t ch, ba; uint32_t sr; std::memcpy(&ch, fmt + 2, 2); std::memcpy(&sr, fmt + 4, 4); std::memcpy(&ba, fmt + 12, 2);
  v->channels = ch; v->rate = sr; v->block_align = ba ? ba : 4;
  { std::lock_guard<std::mutex> l(XA->m); XA->source_voices++; }
  *out = v; return 0;
}
MSABI uint32_t e_CreateSubmixVoice(Engine*, XAudioVoice** out, uint32_t ch, uint32_t sr, uint32_t, uint32_t, void*, void*) {
  if (!out) return 0x80070057u;
  XAudioVoice* v = new_voice(Submix); v->channels = ch; v->rate = sr;
  { std::lock_guard<std::mutex> l(XA->m); XA->submix_voices++; }
  *out = v; return 0;
}
MSABI uint32_t e_CreateMasteringVoice(Engine*, XAudioVoice** out, uint32_t ch, uint32_t sr, uint32_t, const void*, void*, uint32_t) {
  if (!out) return 0x80070057u;
  XAudioVoice* v = new_voice(Mastering); v->channels = ch ? ch : 2; v->rate = sr ? sr : 48000;
  { std::lock_guard<std::mutex> l(XA->m); XA->mastering_voices++; }
  *out = v; return 0;
}
MSABI uint32_t e_StartEngine(Engine*) { std::lock_guard<std::mutex> l(XA->m); XA->engine_running = true; return 0; }
MSABI void e_StopEngine(Engine*) { std::lock_guard<std::mutex> l(XA->m); XA->engine_running = false; }
MSABI void v_GetVoiceDetails(XAudioVoice* v, uint8_t* d) { std::memset(d, 0, 16); std::memcpy(d + 8, &v->channels, 4); std::memcpy(d + 12, &v->rate, 4); }
MSABI uint32_t v_SetVolume(XAudioVoice* v, float f, uint32_t) { v->volume = f; return 0; }
MSABI void v_GetVolume(XAudioVoice* v, float* f) { if (f) *f = v->volume; }
MSABI void v_GetChannelVolumes(XAudioVoice*, uint32_t n, float* f) { for (uint32_t i = 0; f && i < n && i < 8; i++) f[i] = 1.0f; }
MSABI void v_Destroy(XAudioVoice* v) { std::lock_guard<std::mutex> l(XA->m); if (!v->destroyed) { v->destroyed = true; XA->destroyed++; } }
MSABI uint32_t v_Start(XAudioVoice* v, uint32_t, uint32_t) { v->started = true; return 0; }
MSABI uint32_t v_Stop(XAudioVoice* v, uint32_t, uint32_t) { v->started = false; return 0; }
MSABI uint32_t v_Submit(XAudioVoice* v, const uint8_t* b, const void*) {
  if (!b) return 0x80070057u;
  uint32_t bytes, play_len; std::memcpy(&bytes, b + 4, 4); std::memcpy(&play_len, b + 20, 4);
  uint64_t frames = play_len ? play_len : bytes / v->block_align;
  v->samples_played += frames;
  std::lock_guard<std::mutex> l(XA->m); XA->buffers_submitted++; XA->bytes_submitted += bytes;
  return 0;
}
MSABI void v_GetState(XAudioVoice* v, uint8_t* st, uint32_t) { std::memset(st, 0, 24); std::memcpy(st + 16, &v->samples_played, 8); }
MSABI uint32_t v_SetFreq(XAudioVoice* v, float f, uint32_t) { v->freq_ratio = f; return 0; }
MSABI void v_GetFreq(XAudioVoice* v, float* f) { if (f) *f = v->freq_ratio; }
const void* const* voice_vtbl() {
  static const void* t[29];
  static bool init = false;
  if (!init) {
    for (auto& p : t) p = (const void*)(uintptr_t)&e_Ok;
    t[0] = (const void*)(uintptr_t)&v_GetVoiceDetails; t[12] = (const void*)(uintptr_t)&v_SetVolume; t[13] = (const void*)(uintptr_t)&v_GetVolume;
    t[15] = (const void*)(uintptr_t)&v_GetChannelVolumes; t[18] = (const void*)(uintptr_t)&v_Destroy;
    t[19] = (const void*)(uintptr_t)&v_Start; t[20] = (const void*)(uintptr_t)&v_Stop; t[21] = (const void*)(uintptr_t)&v_Submit;
    t[25] = (const void*)(uintptr_t)&v_GetState; t[26] = (const void*)(uintptr_t)&v_SetFreq; t[27] = (const void*)(uintptr_t)&v_GetFreq;
    init = true;
  }
  return t;
}
const void* const* engine_vtbl() {
  static const void* t[13];
  static bool init = false;
  if (!init) {
    auto P = [](auto* f) { return (const void*)(uintptr_t)f; };
    t[0] = P(&e_QueryInterface); t[1] = P(&e_AddRef); t[2] = P(&e_Release); t[3] = P(&e_Ok); t[4] = P(&e_Void);
    t[5] = P(&e_CreateSourceVoice); t[6] = P(&e_CreateSubmixVoice); t[7] = P(&e_CreateMasteringVoice);
    t[8] = P(&e_StartEngine); t[9] = P(&e_StopEngine); t[10] = P(&e_Ok); t[11] = P(&e_Void); t[12] = P(&e_Void);
    init = true;
  }
  return t;
}
MSABI uint32_t xa_Create(void** out, uint32_t, uint32_t) {
  if (!out) return 0x80004003u;   // E_POINTER
  Engine* e = new Engine{engine_vtbl(), 1};
  std::lock_guard<std::mutex> l(XA->m);
  XA->engines.push_back(e); XA->engines_created++;
  *out = e; return 0;
}
}  // namespace
XAudio2Context::~XAudio2Context() { for (void* e : engines) delete (Engine*)e; for (XAudioVoice* v : voices) delete v; }
void XInputContext::set_pad(int i, bool c, uint16_t b, uint8_t l, uint8_t r, int16_t a, int16_t bb, int16_t cc, int16_t d) {
  if (i < 0 || i >= 4) return;
  std::lock_guard<std::mutex> lk(m);
  XInputPad& p = pads[i]; p.connected = c; p.buttons = b; p.lt = l; p.rt = r; p.lx = a; p.ly = bb; p.rx = cc; p.ry = d; p.packet++;
}
bool xinput_register(ImportRegistry& r, XInputContext& ctx) {
  XI = &ctx;
  auto A = [](auto* f) { return (uint64_t)(uintptr_t)f; };
  for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
    r.add(dll, "XInputGetState", A(&x_GetState)); r.add(dll, "XInputSetState", A(&x_SetState));
    r.add(dll, "XInputGetCapabilities", A(&x_GetCapabilities)); r.add(dll, "XInputEnable", A(&x_Enable));
    r.add(dll, "XInputGetBatteryInformation", A(&x_GetBattery)); r.add(dll, "XInputGetKeystroke", A(&x_GetKeystroke));
  }
  return true;
}
bool xaudio2_register(ImportRegistry& r, XAudio2Context& ctx) {
  XA = &ctx;
  for (const char* dll : {"xaudio2_9.dll", "xaudio2_8.dll"}) r.add(dll, "XAudio2Create", (uint64_t)(uintptr_t)&xa_Create);
  return true;
}
}
#else
namespace anyxbox {
XAudio2Context::~XAudio2Context() {}
void XInputContext::set_pad(int, bool, uint16_t, uint8_t, uint8_t, int16_t, int16_t, int16_t, int16_t) {}
bool xinput_register(ImportRegistry&, XInputContext&) { return false; }
bool xaudio2_register(ImportRegistry&, XAudio2Context&) { return false; }
}
#endif
