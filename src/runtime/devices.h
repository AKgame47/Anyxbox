// Anyxbox input and audio devices: XInput pads and an XAudio2 engine that plays into nothing. GPL-2.0.
// Windows x64 calling convention, Linux x86-64 hosts only. No real controller or sound card is touched yet:
// pads are fed by the host (tests, later SDL), and XAudio2 voices consume submitted buffers without producing sound.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include "syslayer/import_registry.h"
namespace anyxbox {
struct XInputPad {
  bool connected = false;
  uint16_t buttons = 0; uint8_t lt = 0, rt = 0; int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  uint32_t packet = 0;
  uint16_t rumble_left = 0, rumble_right = 0;   // last XInputSetState values
};
struct XInputContext {
  XInputPad pads[4];
  bool enabled = true;
  std::mutex m;
  // Host side: change a pad and bump its packet number.
  void set_pad(int i, bool connected, uint16_t buttons, uint8_t lt = 0, uint8_t rt = 0, int16_t lx = 0, int16_t ly = 0, int16_t rx = 0, int16_t ry = 0);
};
struct XAudioVoice;
struct XAudio2Context {
  std::mutex m;
  std::vector<XAudioVoice*> voices;  // never freed before the context (DestroyVoice only marks)
  std::vector<void*> engines;
  int engines_created = 0, source_voices = 0, mastering_voices = 0, submix_voices = 0, destroyed = 0;
  uint64_t buffers_submitted = 0, bytes_submitted = 0;
  bool engine_running = false;
  XAudio2Context() = default;
  XAudio2Context(const XAudio2Context&) = delete;
  XAudio2Context& operator=(const XAudio2Context&) = delete;
  ~XAudio2Context();
};
// XInputGetState/SetState/GetCapabilities/Enable/GetBatteryInformation/GetKeystroke for xinput1_4, xinput1_3 and xinput9_1_0.
// Unconnected pads return ERROR_DEVICE_NOT_CONNECTED (1167), like a PC with no controller.
bool xinput_register(ImportRegistry& reg, XInputContext& ctx);
// XAudio2Create (xaudio2_9.dll, xaudio2_8.dll): IXAudio2 with source, submix and mastering voices (XAudio2 2.8+ vtable layout).
// Submitted buffers count as played at once (BuffersQueued is always 0, SamplesPlayed grows); buffer callbacks are not called yet.
bool xaudio2_register(ImportRegistry& reg, XAudio2Context& ctx);
}
