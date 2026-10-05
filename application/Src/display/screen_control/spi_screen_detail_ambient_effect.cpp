#include "leds/lighting_resources.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "storagemanager.hpp"
namespace {
XoraResource::Ref refs[32];
const char *labels[33];
unsigned entries() {
  labels[0] = "Follow Button LED";
  return LightingResources::list(true, refs, labels + 1, 32) + 1;
}
} // namespace
uint8_t ScreenDetailAmbientEffect_InitIndex() {
  auto *p = STORAGE_MANAGER.getDefaultGamepadProfile();
  if (!p)
    return 0;
  if (p->ledsConfigs.aroundLedSyncToMainLed)
    return 0;
  unsigned n = entries();
  auto current = LightingResources::reference(p, true);
  for (unsigned i = 1; i < n; i++)
    if (!memcmp(&refs[i - 1], &current, sizeof(current)))
      return i;
  return 0;
}
void ScreenDetailAmbientEffect_Rotate(uint8_t *index, int8_t det) {
  if (!index)
    return;
  int n = entries(), v = int(*index) + det;
  *index = v < 0 ? 0 : v >= n ? n - 1 : v;
}
void ScreenDetailAmbientEffect_Render(ST7789_Handle *lcd, uint8_t index,
                                      const ScreenUiStyle &style) {
  unsigned n = entries();
  uint8_t selected = ScreenDetailAmbientEffect_InitIndex();
  ScreenDetailRender_List(lcd, "Ambient Effect", labels, n, index, selected,
                          style);
}
void ScreenDetailAmbientEffect_OnConfirm(uint8_t index) {
  auto *p = STORAGE_MANAGER.getDefaultGamepadProfile();
  unsigned n = entries();
  if (!p || index >= n)
    return;
  if (!index) {
    p->ledsConfigs.aroundLedSyncToMainLed = true;
    ScreenUI_RequestDeferredSave(2000u);
    return;
  }
  if (LightingResources::select(p, true, refs[index - 1], false)) {
    p->ledsConfigs.aroundLedSyncToMainLed = false;
    ScreenUI_RequestDeferredSave(2000u);
  }
}
