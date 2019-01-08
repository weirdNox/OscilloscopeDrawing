typedef struct {
    u8 X, Y;
} point;

typedef struct {
    u32 RepeatCount;
    u32 PointCount;
    point Points[MaxPointsPerFrame];
} frame;

typedef struct {
    u32 FrameCount;
    frame Frames[MaxFrames];
} animation;

// NOTE(nox): For the data, AnimationData.h needs to define:
// static animation Animations[AnimCount] = {...};
enum { AnimCount = 2 };
#include "AnimationData.h"
