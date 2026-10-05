#include "leds/lighting_resources.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "storagemanager.hpp"
namespace {
XoraResource::Ref refs[32];
const char *labels[32];
unsigned entries() { return LightingResources::list(false, refs, labels, 32); }
} // namespace
uint8_t ScreenDetailLightEffect_InitIndex() {
  auto *p = STORAGE_MANAGER.getDefaultGamepadProfile();
  if (!p)
    return 0;

  unsigned n = entries();
  auto current = LightingResources::reference(p, false);
  for (unsigned i = 0; i < n; i++)
    if (!memcmp(&refs[i], &current, sizeof(current)))
      return i;
  return 0;
}
void ScreenDetailLightEffect_Rotate(uint8_t *index, int8_t det) {
  if (!index)
    return;
  int n = entries(), v = int(*index) + det;
  *index = v < 0 ? 0 : v >= n ? n - 1 : v;
}
void ScreenDetailLightEffect_Render(ST7789_Handle *lcd, uint8_t index,
                                    const ScreenUiStyle &style) {
  unsigned n = entries();
  uint8_t selected = ScreenDetailLightEffect_InitIndex();
  ScreenDetailRender_List(lcd, "Light Effect", labels, n, index, selected,
                          style);
}
void ScreenDetailLightEffect_OnConfirm(uint8_t index) {
  auto *p = STORAGE_MANAGER.getDefaultGamepadProfile();
  unsigned n = entries();
  if (!p || index >= n)
    return;

  if (LightingResources::select(p, false, refs[index], false))
    ScreenUI_RequestDeferredSave(2000u);
}
