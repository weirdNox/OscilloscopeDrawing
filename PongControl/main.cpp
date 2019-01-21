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


enum {
    UpdateDeltaMs = 33,
    PadDelta = 2,
};

typedef enum {
    PlayerControl_None,
    PlayerControl_Up,
    PlayerControl_Down,
} player_control;

typedef enum {
    Game_WaitingForInput,
    Game_Playing,
} game_state;

typedef struct {
     player_control Player1;
     player_control Player2;
} controls;

static inline u64 getTimeMs() {
    timespec Spec;
    clock_gettime(CLOCK_MONOTONIC, &Spec);
    return Spec.tv_nsec / 1000000;
}

int main(int, char**) {
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
    u32 Pad1Center, Pad2Center;
    u64 LastUpdateTime = getTimeMs();
    game_state State;
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
                    Pad1Center = Pad2Center = GridSize/2;
                    State = Game_WaitingForInput;
                }
            }
        }
        else {
            // NOTE(nox): Game processing
            u64 Time = getTimeMs();
            if(Time - LastUpdateTime > UpdateDeltaMs) {
                LastUpdateTime = Time;

                controls Controls = {};
                if(ImGui::IsKeyDown(GLFW_KEY_W)) {
                    Controls.Player1 = PlayerControl_Up;
                }
                else if(ImGui::IsKeyDown(GLFW_KEY_S)) {
                    Controls.Player1 = PlayerControl_Down;
                }

                if(ImGui::IsKeyDown(GLFW_KEY_UP)) {
                    Controls.Player2 = PlayerControl_Up;
                }
                else if(ImGui::IsKeyDown(GLFW_KEY_DOWN)) {
                    Controls.Player2 = PlayerControl_Down;
                }

                if(State == Game_WaitingForInput) {
                    if(Controls.Player1 || Controls.Player2) {
                        State = Game_Playing;
                    }
                }

                if(State == Game_Playing) {
                    if(Controls.Player1 == PlayerControl_Up) {
                        Pad1Center += PadDelta;
                    }
                    else if(Controls.Player1 == PlayerControl_Down) {
                        Pad1Center -= PadDelta;
                    }
                    Pad1Center = clamp(0, Pad1Center, GridSize-1);

                    if(Controls.Player2 == PlayerControl_Up) {
                        Pad2Center += PadDelta;
                    }
                    else if(Controls.Player2 == PlayerControl_Down) {
                        Pad2Center -= PadDelta;
                    }
                    Pad2Center = clamp(0, Pad2Center, GridSize-1);
                }

                buff Buff = {};
                writePongUpdate(&Buff, Pad1Center, Pad2Center, 32, 32);
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
