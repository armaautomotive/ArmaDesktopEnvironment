#pragma once
#include <stdbool.h>

int  ade_decor_tab_height(void);
void ade_decor_tab_color(bool selected, float out_rgba4[4]);

int  ade_decor_tab_min_width(void);
int  ade_decor_tab_max_width(void);

int  ade_decor_title_px(void);
int  ade_decor_title_y(void);
void ade_decor_title_ink_rgb(float out_rgb3[3]);

void ade_decor_icon_label_params(int *out_gap_px, int *out_scale, int *out_spacing, int *out_y_nudge);
void ade_decor_icon_label_colors(float out_shadow_rgba4[4], float out_text_rgba4[4]);
