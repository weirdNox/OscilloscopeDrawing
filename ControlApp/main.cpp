#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include <gl3w.c>
#include <glfw/include/GLFW/glfw3.h>

#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>

#include <common.h>
#include <protocol.hpp>
#include "imgui_extensions.cpp"

#define xCoord(Idx, GridSize) (Idx % GridSize)
#define yCoord(Idx, GridSize) (GridSize - (Idx / GridSize) - 1)
#define frameRepeatCount(FrameMs, Fps) ((FrameMs*Fps)/1000)

enum {
    DefaultFrameTimeMs = 1000,
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

typedef struct {
    int Tty;
    int SelectedAnimation;
} serial_ctx;

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

static inline void serialDisconnect(serial_ctx *Ctx) {
    close(Ctx->Tty);
    Ctx->Tty = -1;
    Ctx->SelectedAnimation = 0;
}

static void sendBuffer(buff *Buffer, int SerialTTY) {
    assert(SerialTTY >= 0);

    buff Encoded = {};
    finalizePacket(Buffer, &Encoded);
    u8 Delimiter = 0;
    write(SerialTTY, &Delimiter, 1);
    write(SerialTTY, Encoded.Data, Encoded.Write);
}

static inline u32 calculateFps(u32 PointCount) {
    // NOTE(nox): Assuming each point takes 100us
    return 1000/(PointCount/10 + 3);
}

static void readFileToBuffer(FILE *File, buff *Buff) {
    fseek(File, 0, SEEK_END);
    u64 FileLength = ftell(File);
    rewind(File);

    assert(FileLength <= MaxPacketSize);
    fread(Buff->Data, FileLength, 1, File);
    Buff->Write = FileLength;
}

static void writeBufferToFile(buff *Buff, FILE *File) {
    fwrite(Buff->Data, Buff->Write, 1, File);
}

static void reset(frame *Frames, s32 *FrameCount, s32 *SelectedFrame, s32 *LastSelected) {
    for(int I = 0; I < MaxFrames; ++I) {
        frame *Frame = Frames + I;
        Frame->ActiveCount = 0;
        for(int I = 0; I < GridSize*GridSize; ++I) {
            point *Point = Frame->Points + I;
            Point->Active = false;
            Point->DisablePathBefore = false;
        }
        Frame->NumMilliseconds = DefaultFrameTimeMs;
    }
    *FrameCount = 1;
    *SelectedFrame = 1;
    *LastSelected = -1;
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


    serial_ctx Serial = {-1, 0};

    s32 FrameCount = 1;
    s32 SelectedFrame = 1;
    frame Frames[MaxFrames] = {0};
    for(int I = 0; I < MaxFrames; ++I) {
        Frames[I].NumMilliseconds = DefaultFrameTimeMs;
    }
    bool OnionSkinning = false;
    bool ShowPath = true;
    s32 LastSelected = -1;

    enum { MaxFiles = 30, FileNameMaxLength = 100 };
    u32 NumFiles;
    char Files[MaxFiles][FileNameMaxLength];
    s32 SelectedFile;

    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // NOTE(nox): Unplug detection
        if(Serial.Tty >= 0) {
            termios Conf;
            if(tcgetattr(Serial.Tty, &Conf)) {
                serialDisconnect(&Serial);
            }
        }

        // NOTE(nox): Debug messages from the PIC32
        int N;
        char Buff[200];
        if(Serial.Tty >= 0 && (N = read(Serial.Tty, Buff, sizeof(Buff)))) {
            printf("%.*s", N, Buff);
            fflush(stdout);
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
            if(ImGui::GridSquare(Point->Active, (OnionSkinning && PrevFrame) ? PrevFrame->Points[I].Active : 0,
                                 &Hovered))
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
        // NOTE(nox): Animation settings
        ImGui::Begin("Animation settings", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SliderInt("Frame count", &FrameCount, 1, MaxFrames);
        FrameCount = clamp(1, FrameCount, MaxFrames);

        s32 OldSelectedFrame = SelectedFrame;
        ImGui::SliderInt("Selected frame", &SelectedFrame, 1, FrameCount);
        SelectedFrame = clamp(1, SelectedFrame, FrameCount);
        if(OldSelectedFrame != SelectedFrame) {
            LastSelected = -1;
        }

        if(ImGui::Button("Completely clear animation")) {
            reset(Frames, &FrameCount, &SelectedFrame, &LastSelected);
        }

        // NOTE(nox): This part is really messy and assumes everything is going to work without error
        // checks besides the asserts for space.
        if(ImGui::Button("Save")) {
            ImGui::OpenPopup("Save");
        }

        ImGui::SameLine();

        if(ImGui::Button("Load")) {
            DIR *Dir = opendir("../animations");
            if(Dir) {
                NumFiles = 0;
                SelectedFile = 0;
                dirent *Entry;
                while(Entry = readdir(Dir)) {
                    u32 NameLength = strlen(Entry->d_name);
                    if(NameLength <= FileNameMaxLength && NameLength > 5 &&
                       strcmp(Entry->d_name + NameLength - 5, ".anim") == 0)
                    {
                        strncpy(Files[NumFiles++], Entry->d_name, FileNameMaxLength);
                    }
                }
                if(NumFiles) {
                    ImGui::OpenPopup("Load");
                }
            }
        }

        if(ImGui::BeginPopupModal("Save", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            static char Name[FileNameMaxLength-5] = {};
            ImGui::InputText("Name", Name, arrayCount(Name));

            ImGui::Separator();

            if(ImGui::Button("Save")) {
                char FilePath[FileNameMaxLength+50];
                sprintf(FilePath, "../animations/%s.anim", Name);
                FILE *File = fopen(FilePath, "wb");
                if(File) {
                    buff Buff = {};

                    // NOTE(nox): Write protocol to file
                    writeU8(&Buff, MagicNumber);
                    writeU16(&Buff, 0); // NOTE(nox): Placeholder for length
                    writeU32(&Buff, FrameCount);
                    for(u32 I = 0; I < FrameCount; ++I) {
                        frame *Frame = Frames + I;
                        writeU32(&Buff, Frame->NumMilliseconds);
                        writeU32(&Buff, Frame->ActiveCount);
                        for(u32 J = 0; J < Frame->ActiveCount; ++J) {
                            u32 Index = Frame->Order[J];
                            writeU32(&Buff, Index);
                            writeU8(&Buff, Frame->Points[Index].DisablePathBefore);
                        }
                    }
                    *((u16 *)(Buff.Data + 1)) = Buff.Write-3;

                    writeBufferToFile(&Buff, File);
                    fclose(File);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if(ImGui::BeginPopupModal("Load", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            char *Entries[MaxFiles];
            for(u32 I = 0; I < NumFiles; ++I) {
                Entries[I] = Files[I];
            }
            ImGui::ListBox("Which animation?", &SelectedFile, Entries, NumFiles);

            ImGui::Separator();

            if(ImGui::Button("Load")) {
                char FilePath[FileNameMaxLength+50];
                sprintf(FilePath, "../animations/%s", Entries[SelectedFile]);
                FILE *File = fopen(FilePath, "rb");
                if(File) {
                    buff Buff = {};
                    readFileToBuffer(File, &Buff);

                    // NOTE(nox): Parse protocol out of file
                    u8 MagicTest = readU8(&Buff);
                    if(MagicTest == MagicNumber) {
                        u16 Length = readU16(&Buff);
                        if(hasAvailable(&Buff, Length)) {
                            reset(Frames, &FrameCount, &SelectedFrame, &LastSelected);
                            FrameCount = readU32(&Buff);
                            for(u32 I = 0; I < FrameCount; ++I) {
                                frame *Frame = Frames + I;
                                Frame->NumMilliseconds = readU32(&Buff);
                                Frame->ActiveCount = readU32(&Buff);
                                for(u32 J = 0; J < Frame->ActiveCount; ++J) {
                                    u32 Index = readU32(&Buff);
                                    point *Point = Frame->Points + Index;
                                    Point->Active = true;
                                    Point->DisablePathBefore = readU8(&Buff);
                                    Frame->Order[J] = Index;
                                }
                            }
                        }
                    }

                    fclose(File);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
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
        ImGui::SameLine();
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
        // NOTE(nox): Control
        ImGui::Begin("Control", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        if(Serial.Tty < 0) {
            if(ImGui::Button("Connect")) {
                Serial.Tty = serialConnect();
            }
        }
        else {
            if(ImGui::Button("Disconnect")) {
                serialDisconnect(&Serial);
            }

            if(ImGui::Button("Power on")) {
                buff Buff = {};
                writePowerOn(&Buff);
                sendBuffer(&Buff, Serial.Tty);
            }
            ImGui::SameLine();
            if(ImGui::Button("Power off")) {
                buff Buff = {};
                writePowerOff(&Buff);
                sendBuffer(&Buff, Serial.Tty);
            }

            int OldAnim = Serial.SelectedAnimation;
            ImGui::RadioButton("Animation 1", &Serial.SelectedAnimation, 0); ImGui::SameLine();
            ImGui::RadioButton("Animation 2", &Serial.SelectedAnimation, 1);
            if(Serial.SelectedAnimation != OldAnim) {
                buff Buff = {};
                writeSelectAnim(&Buff, Serial.SelectedAnimation);
                sendBuffer(&Buff, Serial.Tty);
            }

            if(ImGui::Button("Upload animation")) {
                for(u8 I = 0; I < FrameCount; ++I) {
                    frame *Frame = Frames + I;
                    u32 Fps = calculateFps(Frame->ActiveCount);
                    buff Buff = {};
                    writeHeader(&Buff, Command_UpdateFrame);
                    writeU8(&Buff, I);
                    writeU16(&Buff, Fps);
                    writeU16(&Buff, frameRepeatCount(Frame->NumMilliseconds, Fps));
                    writeU16(&Buff, Frame->ActiveCount);

                    for(int J = 0; J < Frame->ActiveCount; ++J) {
                        int ActiveIndex = Frame->Order[J];
                        point *Point = Frame->Points + ActiveIndex;
                        int X = xCoord(ActiveIndex, GridSize) | (Point->DisablePathBefore ? ZDisableBit : 0);
                        int Y = yCoord(ActiveIndex, GridSize);
                        writeU8(&Buff, X);
                        writeU8(&Buff, Y);
                    }

                    sendBuffer(&Buff, Serial.Tty);
                }

                buff Buff = {};
                writeUpdateFrameCount(&Buff, FrameCount);
                sendBuffer(&Buff, Serial.Tty);
            }

            ImGui::SameLine();
            if(ImGui::Button("Upload test")) {
                u8 FrameCount = 0;
#define frameBuff(Name, Num)                                \
                buff Name = {};                             \
                writeHeader(&Name, Command_UpdateFrame);    \
                writeU8(&Name,  Num);                       \
                writeU16(&Name,  30);                       \
                writeU16(&Name,  20);                       \
                writeU16(&Name, 300);                       \
                ++FrameCount;

                frameBuff(Frame0, 0);
                frameBuff(Frame1, 1);
                frameBuff(Frame2, 2);
                frameBuff(Frame3, 3);
#undef frameBuff

                for(int I = 0; I < 300; ++I) {
                    u8 Val = I % 64; if(Val == 0) Val = 1;

                    writeU8(&Frame0, Val);
                    writeU8(&Frame0, Val);

                    writeU8(&Frame1, Val);
                    writeU8(&Frame1, 32);

                    writeU8(&Frame2, Val);
                    writeU8(&Frame2, 63-Val);

                    writeU8(&Frame3, 32);
                    writeU8(&Frame3, 63-Val);
                }

                sendBuffer(&Frame0, Serial.Tty);
                sendBuffer(&Frame1, Serial.Tty);
                sendBuffer(&Frame2, Serial.Tty);
                sendBuffer(&Frame3, Serial.Tty);

                buff FrameCountBuff = {};
                writeUpdateFrameCount(&FrameCountBuff, FrameCount);
                sendBuffer(&FrameCountBuff, Serial.Tty);
            }

            if(ImGui::Button("Set to 0 after drawing")) {
                buff Buff = {};
                writeSetTo0(&Buff);
                sendBuffer(&Buff, Serial.Tty);
            }

            if(ImGui::Button("Don't set to 0 after drawing")) {
                buff Buff = {};
                writeDontSetTo0(&Buff);
                sendBuffer(&Buff, Serial.Tty);
            }

            if(ImGui::Button("Info light on")) {
                buff Buff = {};
                writeInfoLedOn(&Buff);
                sendBuffer(&Buff, Serial.Tty);
            }
            ImGui::SameLine();
            if(ImGui::Button("Info light off")) {
                buff Buff = {};
                writeInfoLedOff(&Buff);
                sendBuffer(&Buff, Serial.Tty);
            }
        }

        ImGui::Separator();

        if(ImGui::Button("Copy C array to clipboard!")) {
#define I1 "    "
#define I2 "      "
#define I3 "          "
#define I4 "              "
#define I5 "                 "
            ImGui::LogToClipboard();
            ImGui::LogText(I1 "{ %d,\n" I2 "{", FrameCount);
            for(int I = 0; I < FrameCount; ++I) {
                frame *Frame = Frames + I;
                u32 Fps = calculateFps(Frame->ActiveCount);
                ImGui::LogText("\n" I3 "{\n" I4 "%d, %d, %d, {", Fps,
                               frameRepeatCount(Frame->NumMilliseconds, Fps), Frame->ActiveCount);
                for(int J = 0; J < Frame->ActiveCount; ++J) {
                    if((J % 7) == 0) {
                        ImGui::LogText("\n" I5);
                    }
                    int ActiveIndex = Frame->Order[J];
                    point *Point = Frame->Points + ActiveIndex;
                    int X = xCoord(ActiveIndex, GridSize);
                    int Y = yCoord(ActiveIndex, GridSize);
                    ImGui::LogText(" {%d, %d},", X | (Point->DisablePathBefore ? ZDisableBit : 0), Y);
                }
                ImGui::LogText("\n" I4 "}\n" I3 "},");
            }
            ImGui::LogText("\n" I2 "}\n" I1 "},");
            ImGui::LogFinish();
        }
        ImGui::End();

        // ------------------------------------------------------------------------------------------
        // NOTE(nox): Drawing utilities
        ImGui::Begin("Drawing utilities", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Checkbox("Onion skinning", &OnionSkinning);
        ImGui::Checkbox("Show path", &ShowPath);
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
