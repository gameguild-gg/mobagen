#ifndef IMGUIEXTRA_H
#define IMGUIEXTRA_H

#include <cfloat>

// Plot value over time
// Pass FLT_MAX value to draw without adding a new value
void	PlotVar(const char* label, float value, float scale_min = FLT_MAX, float scale_max = FLT_MAX, size_t buffer_size = 120);

// Call this periodically to discard old/unused data
void	PlotVarFlushOldEntries();

//Tooltip from imgui_demo
void HelpMarker(const char* desc);

void ImguiTooltip(const char* desc);

#endif IMGUIEXTRA_H
