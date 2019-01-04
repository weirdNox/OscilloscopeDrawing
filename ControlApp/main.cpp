#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include <gl3w.c>
#include <glfw/include/GLFW/glfw3.h>

#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include <common.h>
#include <protocol.h>
#include "imgui_extensions.cpp"

enum {
    GridSize = 64,
    MaxFrames = 10,
    MaxActive = 300, // NOTE(nox): Limit to achieve 30 FPS
    FPS = 30,
    MinFrameTimeMs = (1000 + FPS - 1)/FPS
};

typedef struct {
    bool Active;
    bool DisablePathBefore;
} point;

typedef struct {
    u32 ActiveCount;
    u32 Order[MaxActive];
    point Points[GridSize*GridSize];
    ImVec2 Pos[GridSize*GridSize];
    s32 NumMilliseconds;
} frame;

static void glfwErrorCallback(int Error, const char* Description) {
    fprintf(stderr, "GLFW Error %d: %s\n", Error, Description);
}

static s32 min(s32 A, s32 B) {
    return A < B ? A : B;
}

static s32 max(s32 A, s32 B) {
    return A > B ? A : B;
}

static int serialConnect() {
    int SerialTTY = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);

    if(!isatty(SerialTTY)) {
        goto connectionError;
    }

    termios Config;
    if(tcgetattr(SerialTTY, &Config) < 0) {
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

    // NOTE(nox): Non blocking, return immediately what is available
    Config.c_cc[VMIN]  = 0;
    Config.c_cc[VTIME] = 0;

    if(cfsetispeed(&Config, B115200) < 0 || cfsetospeed(&Config, B115200) < 0) {
        goto connectionError;
    }
    if(tcsetattr(SerialTTY, TCSAFLUSH, &Config) < 0) {
        goto connectionError;
    }

    return SerialTTY;

  connectionError:
    close(SerialTTY);
    return -1;
}

static inline void serialDisconnect(int *SerialTTY) {
    close(*SerialTTY);
    *SerialTTY = -1;
}

static void sendBuffer(uart_buff *Buffer, int SerialTTY) {
    assert(SerialTTY >= 0);

    uart_buff Encoded = {};
    stuffBytes(Buffer, &Encoded);
    u8 Delimiter = 0;
    write(SerialTTY, &Delimiter, 1);
    write(SerialTTY, Encoded.Data, Encoded.Index);
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
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Control Application", NULL, NULL);
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

    int SerialTTY = -1;
    s32 FrameCount = 1;
    s32 SelectedFrame = 1;
    frame Frames[MaxFrames] = {0};
    bool OnionSkinning = false;
    bool ShowPath = true;
    s32 LastSelected = -1;
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // NOTE(nox): Unplug detection
        if(SerialTTY >= 0) {
            termios Conf;
            if(tcgetattr(SerialTTY, &Conf)) {
                close(SerialTTY);
                SerialTTY = -1;
            }
        }

        frame *Frame = Frames + SelectedFrame - 1;
        frame *PrevFrame = SelectedFrame > 1 ? Frames + SelectedFrame - 2 : 0;

        s32 ToHighlight = LastSelected;


        // ------------------------------------------------------------------------------------------
        // NOTE(nox): Grid
        ImGui::Begin("Grid", 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        for(int I = 0; I < GridSize*GridSize; I++) {
            ImGui::PushID(I);
            point *Point = Frame->Points + I;
            Frame->Pos[I] = ImGui::GetCursorScreenPos();
            bool Hovered;
            if(ImGui::GridSquare(Point->Active, (OnionSkinning && PrevFrame) ? PrevFrame->Points[I].Active : 0, &Hovered))
            {
                LastSelected = I;
                if(!Point->Active && Frame->ActiveCount < MaxActive) {
                    Frame->Order[Frame->ActiveCount++] = I;
                    Point->Active = true;
                }
            }

            if(Hovered && Point->Active) {
                ToHighlight = I;
            }

            if((I % GridSize) < GridSize-1) {
                ImGui::SameLine();
            }
            ImGui::PopID();
        }

        if(ShowPath) {
            for(u32 I = 1; I < Frame->ActiveCount; ++I) {
                ImVec2 Pos1 = Frame->Pos[Frame->Order[I-1]];
                ImVec2 Pos2 = Frame->Pos[Frame->Order[I]];
                u8 Alpha = Frame->Points[Frame->Order[I]].DisablePathBefore ? 50 : 255;
                ImU32 Col = Frame->Order[I] == ToHighlight ? IM_COL32(255, 255, 255, Alpha) : IM_COL32(255, 0, 0, Alpha);
                DrawList->AddLine(ImVec2(Pos1.x + 2, Pos1.y + 4), ImVec2(Pos2.x + 2, Pos2.y + 4), Col);
            }
        }
        ImGui::End();


        // ------------------------------------------------------------------------------------------
        // NOTE(nox): General settings
        ImGui::Begin("Animation settings", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SliderInt("Frame count", &FrameCount, 1, MaxFrames);
        FrameCount = clamp(1, FrameCount, MaxFrames);

        s32 OldSelectedFrame = SelectedFrame;
        ImGui::SliderInt("Selected frame", &SelectedFrame, 1, FrameCount);
        SelectedFrame = clamp(1, SelectedFrame, FrameCount);
        if(OldSelectedFrame != SelectedFrame) {
            LastSelected = -1;
        }

        ImGui::Checkbox("Onion skinning", &OnionSkinning);
        ImGui::Checkbox("Show path", &ShowPath);
        ImGui::End();


        // ------------------------------------------------------------------------------------------
        // NOTE(nox): Frame settings
        ImGui::Begin("Frame settings", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Selected frame: %d out of %d", SelectedFrame, FrameCount);
        ImGui::Text("Number of points: %d out of %d", Frame->ActiveCount, MaxActive);

        ImGui::SliderInt("Time (ms)", &Frame->NumMilliseconds, MinFrameTimeMs, 2000);
        Frame->NumMilliseconds = max(MinFrameTimeMs, Frame->NumMilliseconds);

        if(ImGui::Button("Clear frame")) {
            Frame->ActiveCount = 0;
            for(int I = 0; I < GridSize*GridSize; ++I) {
                point *Point = Frame->Points + I;
                Point->Active = false;
                Point->DisablePathBefore = false;
            }
            LastSelected = -1;
        }

        if(ImGui::Button("\"Optimize\" path") && Frame->ActiveCount > 0) {
            u32 NumVisited = 0;
            bool Visited[GridSize*GridSize] = {0};

            u32 Current = Frame->Order[0];
            Visited[Current] = true;
            ++NumVisited;

            while(NumVisited < Frame->ActiveCount) {
                u32 CurrentX = Current % GridSize;
                u32 CurrentY = Current / GridSize;

                u32 Best;
                float BestDist = FLT_MAX;
                for(u32 Index = 0; Index < GridSize*GridSize; ++Index) {
                    if(Frame->Points[Index].Active && !Visited[Index]) {
                        s32 DeltaX = (Index % GridSize) - CurrentX;
                        s32 DeltaY = (Index / GridSize) - CurrentY;
                        float Dist = sqrt(DeltaX*DeltaX + DeltaY*DeltaY);
                        if(Dist < BestDist) {
                            Best = Index;
                            BestDist = Dist;
                        }
                    }
                }

                Frame->Order[NumVisited++] = Best;
                Visited[Best] = true;
                Current = Best;
            }

            for(u32 I = 0; I < Frame->ActiveCount; ++I) {
                Frame->Points[Frame->Order[I]].DisablePathBefore = false;
            }

            ShowPath = true;
        }
        ImGui::End();


        // ------------------------------------------------------------------------------------------
        // NOTE(nox): Point settings
        ImGui::Begin("Point settings", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        if(LastSelected >= 0) {
            point *Point = &Frame->Points[LastSelected];
            ImGui::Text("Point %d", LastSelected);
            ImGui::Checkbox("Disable drawing before moving to this point", &Point->DisablePathBefore);
            if(ImGui::Button("Delete point")) {
                u32 OrderIdx;
                for(OrderIdx = 0; OrderIdx < Frame->ActiveCount; ++OrderIdx) {
                    if(Frame->Order[OrderIdx] == LastSelected) {
                        break;
                    }
                }
                memcpy(Frame->Order + OrderIdx, Frame->Order + OrderIdx + 1,
                       sizeof(*Frame->Order)*(MaxActive-1-OrderIdx));
                --Frame->ActiveCount;
                *Point = (point){0};
                LastSelected = -1;
            }
        } else {
            ImGui::Text("No point is selected!");
        }
        ImGui::End();


        // ------------------------------------------------------------------------------------------
        // NOTE(nox): Commands
        ImGui::Begin("Commands", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        if(SerialTTY < 0) {
            if(ImGui::Button("Connect")) {
                SerialTTY = serialConnect();
            }
        }
        else {
            if(ImGui::Button("Power on")) {
                uart_buff Buff = {};
                writePowerOn(&Buff);
                sendBuffer(&Buff, SerialTTY);
            }
            ImGui::SameLine();
            if(ImGui::Button("Power off")) {
                uart_buff Buff = {};
                writePowerOff(&Buff);
                sendBuffer(&Buff, SerialTTY);
            }

            if(ImGui::Button("Disconnect")) {
                serialDisconnect(&SerialTTY);
            }
        }

        ImGui::Separator();

        if(ImGui::Button("Export to C header")) {
            FILE *File = fopen("generated_header.h", "w");
            fprintf(File, "frame Frames[] = {");
            for(int I = 0; I < FrameCount; ++I) {
                frame *Frame = Frames + I;
                fprintf(File, "\n    {\n        %d, %d, {", (Frame->NumMilliseconds*FPS)/1000,
                        Frame->ActiveCount);
                for(int J = 0; J < Frame->ActiveCount; ++J) {
                    if((J % 7) == 0) {
                        fprintf(File, "\n           ");
                    }
                    int ActiveIndex = Frame->Order[J];
                    point *Point = Frame->Points + ActiveIndex;
                    int X = (4096*(ActiveIndex % GridSize)) / GridSize;
                    int Y = (4096*(GridSize - (ActiveIndex / GridSize) - 1)) / GridSize;
                    fprintf(File, " {%d, %d},", X | (Point->DisablePathBefore ? 1<<12 : 0), Y);
                }
                fprintf(File, "\n        }\n    },");
            }
            fprintf(File, "\n};");
            fclose(File);
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
