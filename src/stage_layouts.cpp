// Единственная TU, в которой живут определения карт стен стейджей. Остальные
// TU включают assets/stage_layouts.h без STAGE_LAYOUTS_DEFINE и получают extern
// объявления: это экономит 824 байта flash на карту от дублирования.
#define STAGE_LAYOUTS_DEFINE
#include "assets/stage_layouts.h"