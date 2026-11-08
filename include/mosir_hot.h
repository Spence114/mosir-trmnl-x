#pragma once
#include <types.h>
#if defined(BOARD_TRMNL_X) && defined(HOT_MODE)
bool mosir_hot_eligible();
void mosir_hot_run(https_request_err_e initialResult);
bool display_show_hot_png(uint8_t *data, int size, bool full);
void display_hot_invalidate();
bool display_hot_clean();
#else
inline bool mosir_hot_eligible() { return false; }
inline void mosir_hot_run(https_request_err_e) {}
#endif
