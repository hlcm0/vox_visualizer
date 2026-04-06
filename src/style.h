#pragma once

/* Rendering constants matching renderer/style.py */

#define LANE_WIDTH            80
#define PIXELS_PER_SECOND     300
#define MARGIN                20

/* Colors: {R, G, B, A} */
#define COL_BACKGROUND        {0,   0,   0,   0}
#define COL_GRID              {200, 200, 200, 255}
#define COL_SUB_GRID          {80,  80,  80,  255}
#define COL_LANE              {0,   0,   0,   255}

#define GRID_WIDTH            2
#define SUB_GRID_WIDTH        2

#define CHIP_HEIGHT           4

#define COL_BT_CHIP_FILL      {248, 248, 248, 255}
#define COL_FX_CHIP_FILL      {190, 170, 40,  255}
#define COL_BT_CHIP_OUTLINE   {150, 150, 150, 255}
#define BT_CHIP_OUTLINE_WIDTH 2
#define COL_FX_CHIP_OUTLINE   {255, 214, 74,  255}
#define FX_CHIP_OUTLINE_WIDTH 2

#define COL_BT_LONG_FILL           {150, 150, 150, 255}
#define COL_BT_LONG_SIDE_OUTLINE   {248, 248, 248, 255}
#define BT_LONG_SIDE_OUTLINE_WIDTH 2
#define COL_FX_LONG_FILL           {190, 170, 40,  176}
#define COL_FX_LONG_SIDE_OUTLINE   {255, 214, 74,  224}
#define FX_LONG_SIDE_OUTLINE_WIDTH 2

#define FX_CHIP_INSET  2
#define BT_CHIP_INSET  2
#define FX_LONG_INSET  2
#define BT_LONG_INSET  4

#define COL_LASER_L_FILL  {0,   140, 255, 120}
#define COL_LASER_R_FILL  {255, 0,   140, 120}
#define LASER_WIDTH       16
#define LASER_HEIGHT      12

#define COL_LASER_START_INDICATOR {255, 255, 255, 255}
#define LASER_START_INDICATOR_WIDTH 2

#define LABEL_FONT_SIZE 18

#define COL_MEASURE_LABEL {220, 220, 220, 255}
#define MEASURE_LABEL_X_OFFSET 10

#define COL_BPM_LABEL {220, 220, 0, 255}
#define BPM_LABEL_X_OFFSET 10
