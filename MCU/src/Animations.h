typedef struct {
    u16 X, Y;
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

enum { AnimCount = 2 };
static animation Animations[AnimCount] = {
    {
        2,
        {
            {
                60, 1, {
                    {128, 128},
                }
            },
            {
                60, 1, {
                    {4032, 4032},
                }
            }
        }
    },
    {
        1,
        {
            {
                60, 1, {
                    {2000, 2000},
                }
            }
        }
    }
};
