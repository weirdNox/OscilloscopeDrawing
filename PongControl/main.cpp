#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include <gl3w.c>
#include <glfw/include/GLFW/glfw3.h>

#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include <common.h>
#include <protocol.hpp>

typedef int serial_ctx;

static void glfwErrorCallback(int Error, const char* Description) {
    fprintf(stderr, "GLFW Error %d: %s\n", Error, Description);
}

static int serialConnect() {
    int Tty = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_NONBLOCK);

    if(!isatty(Tty)) {
        goto connectionError;
    }

    termios Config;
    if(tcgetattr(Tty, &Config) < 0) {
        goto connectionError;
    }


    Config.c_iflag &= ~(INPCK); // NOTE(nox): Disable input parity check
    Config.c_iflag &= ~(IXON | IXOFF | IXANY); // NOTE(nox): Disable software flow control
    Config.c_iflag &= ~(IGNBRK | BRKINT | ISTRIP | INLCR | IGNCR | ICRNL); // NOTE(nox): Disable any special handling of received bytes

    Config.c_oflag  = ~(OPOST | ONLCR); // NOTE(nox): Disable output processing

    Config.c_cflag &= ~(PARENB | CRTSCTS);  // NOTE(nox): Disable parity generation and RTS/CTS flow control
    Config.c_cflag &= ~CSTOPB; // NOTE(nox): Only 1 stop bit
    Config.c_cflag &= ~CSIZE;
    Config.c_cflag |=  CS8; // NOTE(nos): Set 8 bits as communication unit
    Config.c_cflag |=  (CREAD | CLOCAL); // NOTE(nox): Enable receiver and ignore modem lines

    Config.c_lflag &= ~(ICANON | ECHO | ISIG); // NOTE(nox): Disable canonical mode, echos and don't generate signals

    // NOTE(nox): Non blocking, return immediately what is available (ignored due to O_NONBLOCK)
    Config.c_cc[VMIN]  = 0;
    Config.c_cc[VTIME] = 0;

    if(cfsetispeed(&Config, B115200) < 0 || cfsetospeed(&Config, B115200) < 0) {
        goto connectionError;
    }
    if(tcsetattr(Tty, TCSAFLUSH, &Config) < 0) {
        goto connectionError;
    }

    return Tty;

  connectionError:
    close(Tty);
    return -1;
}

static inline void serialDisconnect(serial_ctx *Tty) {
    close(*Tty);
    *Tty = -1;
}

static void sendBuffer(buff *Buffer, int SerialTTY) {
    assert(SerialTTY >= 0);

    buff Encoded = {};
    finalizePacket(Buffer, &Encoded);
    u8 Delimiter = 0;
    write(SerialTTY, &Delimiter, 1);
    write(SerialTTY, Encoded.Data, Encoded.Write);
}


static const u64 UpdateDeltaMs = 33;
static const r32 Pi = 3.1415926535f;
static const r32 BallVel = 2.f;

typedef enum {
    Control_None,
    Control_Up,
    Control_Down,
} paddle_control;

typedef enum {
    Game_WaitingForInput,
    Game_Playing,
} game_state;

typedef struct {
    paddle_control Left;
    paddle_control Right;
} controls;

typedef struct {
    r32 X;
    r32 Y;
} v2;

typedef struct {
    v2 Pos;
    v2 Vel;
} ball;

typedef struct {
    u8 CenterY;
} paddle;

static inline v2 add(v2 A, v2 B) {
    v2 Result = {A.X + B.X, A.Y + B.Y};
    return Result;
}

static inline v2 sub(v2 A, v2 B) {
    v2 Result = {A.X - B.X, A.Y - B.Y};
    return Result;
}

static inline v2 mult(v2 Vec, r32 A) {
    v2 Result = {Vec.X*A, Vec.Y*A};
    return Result;
}

static inline r32 lengthSq(v2 Vec) {
    return Vec.X*Vec.X + Vec.Y*Vec.Y;
}

static inline r32 length(v2 Vec) {
    return sqrt(lengthSq(Vec));
}

static inline v2 normalize(v2 Vec) {
    v2 Result = mult(Vec, 1.0f / length(Vec));
    return Result;
}

static inline r32 cross(v2 A, v2 B) {
    return A.X*B.Y - A.Y*B.X;
}

static inline v2 rotate(v2 Vec, r32 Deg) {
    r32 Rad = Pi * Deg / 180.0f;
    r32 Cos = cos(Rad);
    r32 Sin = sin(Rad);
    v2 Result = {Cos*Vec.X - Sin*Vec.Y, Sin*Vec.X + Cos*Vec.Y};
    return Result;
}

typedef struct {
    bool Exists;
    r32 T, U;
} segment_intersection;
static segment_intersection intersect(v2 Pos1, v2 Dir1, v2 Pos2, v2 Dir2) {
    segment_intersection Result = {};

    r32 Cross = cross(Dir1, Dir2);
    if(Cross != 0) {
        v2 Diff = sub(Pos2, Pos1);
        Result.T = cross(Diff, Dir2)/Cross;
        Result.U = cross(Diff, Dir1)/Cross;

        if((Result.T >= 0.0f && Result.T <= 1.0f) &&
           (Result.U >= 0.0f && Result.U <= 1.0f))
        {
            Result.Exists = true;
        }
    }

    return Result;
}

static inline void randomizeBall(ball *Ball) {
    Ball->Pos.X = Ball->Pos.Y = (GridSize-1)/2.f;
    Ball->Vel = rotate({BallVel*0.5f, 0}, (drand48()*2.0f - 1)*45);
    if(drand48() >= .5f) {
        Ball->Vel = mult(Ball->Vel, -1);
    }
}

static inline u64 getTimeMs() {
    timespec Spec;
    clock_gettime(CLOCK_MONOTONIC, &Spec);
    return Spec.tv_nsec / 1000000;
}

static inline void updatePaddle(paddle *Paddle, paddle_control Control) {
    u8 PaddleDelta = 2;

    if(Control == Control_Up) {
        Paddle->CenterY += PaddleDelta;
    } else if(Control == Control_Down) {
        Paddle->CenterY -= PaddleDelta;
    }

    Paddle->CenterY = clamp(PaddleMinY, Paddle->CenterY, PaddleMaxY);
}

int main(int, char**) {
    srand48(time(0));

    glfwSetErrorCallback(glfwErrorCallback);
    if(!glfwInit()) {
        return 1;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    glfwWindowHint(GLFW_MAXIMIZED, true);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Pong", NULL, NULL);
    if(window == NULL) {
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    bool err = gl3wInit() != 0;
    if(err) {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);


    serial_ctx Serial = -1;

    u64 Time = getTimeMs();
    u64 TimeAccumulator = 0;

    game_state State;
    paddle LeftPaddle, RightPaddle;
    ball Ball;
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // NOTE(nox): Unplug detection
        if(Serial >= 0) {
            termios Conf;
            if(tcgetattr(Serial, &Conf)) {
                serialDisconnect(&Serial);
            }
        }

        ImGui::Begin("Control", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        if(Serial < 0) {
            if(ImGui::Button("Connect")) {
                Serial = serialConnect();
                if(Serial >= 0) {
                    State = Game_WaitingForInput;
                    LeftPaddle.CenterY = RightPaddle.CenterY = GridSize/2;
                    randomizeBall(&Ball);
                }
            }
        }
        else {
            // NOTE(nox): Game processing
            u64 NewTime = getTimeMs();
            TimeAccumulator = min(TimeAccumulator + (NewTime-Time), 4*UpdateDeltaMs);
            Time = NewTime;

            bool DidUpdate = false;
            while(TimeAccumulator >= UpdateDeltaMs) {
                TimeAccumulator -= UpdateDeltaMs;
                DidUpdate = true;

                controls Controls = {};
                if(ImGui::IsKeyDown(GLFW_KEY_W)) {
                    Controls.Left = Control_Up;
                }
                else if(ImGui::IsKeyDown(GLFW_KEY_S)) {
                    Controls.Left = Control_Down;
                }

                if(ImGui::IsKeyDown(GLFW_KEY_UP)) {
                    Controls.Right = Control_Up;
                }
                else if(ImGui::IsKeyDown(GLFW_KEY_DOWN)) {
                    Controls.Right = Control_Down;
                }

                if(State == Game_WaitingForInput) {
                    if(Controls.Left || Controls.Right) {
                        State = Game_Playing;
                    }
                }

                if(State == Game_Playing) {
                    updatePaddle(&LeftPaddle, Controls.Left);
                    updatePaddle(&RightPaddle, Controls.Right);

                    v2 OldPos = Ball.Pos;
                    Ball.Pos = add(Ball.Pos, Ball.Vel);
                    if(Ball.Vel.Y > 0.0f && Ball.Pos.Y > GridSize-1) {
                        Ball.Pos.Y = GridSize-1;
                        Ball.Vel.Y = -Ball.Vel.Y;
                    }
                    else if(Ball.Vel.Y < 0.0f && Ball.Pos.Y < 0) {
                        Ball.Pos.Y = 0;
                        Ball.Vel.Y = -Ball.Vel.Y;
                    }
                    else {
                        v2 WallStart, WallDirection, VelDirection;
                        if(Ball.Vel.X < 0) {
                            WallStart = {LeftPaddleX+0.5f, LeftPaddle.CenterY - PaddleHeight/2.0f};
                            WallDirection = {0, PaddleHeight};
                            VelDirection = {BallVel, 0};
                        }
                        else {
                            WallStart = {RightPaddleX-0.5f, RightPaddle.CenterY + PaddleHeight/2.0f};
                            WallDirection = {0, -PaddleHeight};
                            VelDirection = {-BallVel, 0};
                        }

                        segment_intersection Intersection = intersect(OldPos, Ball.Vel, WallStart, WallDirection);
                        if(Intersection.Exists) {
                            Ball.Pos = add(OldPos, mult(Ball.Vel, Intersection.T));
                            Ball.Vel = rotate(VelDirection, (Intersection.U - 0.5f)*75.0f);
                        }
                    }

                    if((Ball.Pos.X < -0.5f) || (Ball.Pos.X > GridSize-0.5f)) {
                        randomizeBall(&Ball);
                    }
                }
            }

            if(DidUpdate) {
                buff Buff = {};
                writePongUpdate(&Buff, LeftPaddle.CenterY, RightPaddle.CenterY, round(Ball.Pos.X),
                                round(Ball.Pos.Y));
                sendBuffer(&Buff, Serial);
            }

            if(ImGui::Button("Disconnect")) {
                serialDisconnect(&Serial);
            }
        }
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwMakeContextCurrent(window);
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwMakeContextCurrent(window);
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
