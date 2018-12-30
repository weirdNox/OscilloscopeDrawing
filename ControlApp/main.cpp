#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include <gl3w.c>
#include <glfw/include/GLFW/glfw3.h>

#include <stdio.h>

#include "imgui_extensions.cpp"

typedef  uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef   int8_t  s8;
typedef  int16_t s16;
typedef  int32_t s32;

#define arrayCount(Arr) ((sizeof(Arr))/(sizeof(*Arr)))

enum {
    GridSize = 64,
    MaxActive = 300,
    MaxFrames = 10, // NOTE(nox): Limit to achieve 30 FPS
    FPS = 30,
    MinFrameTimeMs = (1000 + FPS - 1)/FPS
};

typedef struct {
    u32 ActiveCount;
    u32 Order[MaxActive];
    bool Active[GridSize*GridSize];
    s32 NumMilliseconds;
} frame;

static void glfwErrorCallback(int Error, const char* Description) {
    fprintf(stderr, "GLFW Error %d: %s\n", Error, Description);
}

static s32 clamp(s32 Min, s32 Val, s32 Max) {
    if(Val < Min) {
        Val = Min;
    } else if(Val > Max) {
        Val = Max;
    }

    return Val;
}

static s32 min(s32 A, s32 B) {
    return A < B ? A : B;
}

static s32 max(s32 A, s32 B) {
    return A > B ? A : B;
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

    s32 FrameCount = 1;
    s32 SelectedFrame = 1;
    frame Frames[MaxFrames] = {0};
    bool OnionSkinning = false;
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        frame *Frame = Frames + SelectedFrame - 1;

        // ------------------------------------------------------------------------------------------
        // NOTE(nox): Grid
        ImGui::Begin("Grid", 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        frame *PrevFrame = SelectedFrame > 1 ? Frames + SelectedFrame - 2 : 0;
        for(int I = 0; I < GridSize*GridSize; I++) {
            ImGui::PushID(I);
            if(ImGui::GridSquare(Frame->Active[I], OnionSkinning && PrevFrame ? PrevFrame->Active[I] : 0) &&
               !Frame->Active[I] && Frame->ActiveCount < MaxActive)
            {
                Frame->Order[Frame->ActiveCount++] = I;
                Frame->Active[I] = true;
            }
            if((I % GridSize) < GridSize-1) {
                ImGui::SameLine();
            }
            ImGui::PopID();
        }
        ImGui::End();

        // ------------------------------------------------------------------------------------------
        // NOTE(nox): General settings
        ImGui::Begin("Animation settings", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SliderInt("Frame count", &FrameCount, 1, MaxFrames);
        FrameCount = clamp(1, FrameCount, MaxFrames);

        ImGui::SliderInt("Selected frame", &SelectedFrame, 1, FrameCount);
        SelectedFrame = clamp(1, SelectedFrame, FrameCount);

        ImGui::Checkbox("Onion skinning", &OnionSkinning);
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
                Frame->Active[I] = false;
            }
        }
        ImGui::End();

        // ------------------------------------------------------------------------------------------
        // NOTE(nox): Commands
        ImGui::Begin("Commands", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        if(ImGui::Button("Export to C header")) {
            FILE *File = fopen("generated_header.h", "w");
            fprintf(File, "frame Frames[] = {");
            for(int I = 0; I < FrameCount; ++I) {
                frame *Frame = Frames + I;
                fprintf(File, "\n    {\n        %d, %d, {", (Frame->NumMilliseconds*FPS)/1000,
                        Frame->ActiveCount);
                for(int J = 0; J < Frame->ActiveCount; ++J) {
                    if((J % 7) == 0) {
                        fprintf(File, "\n            ");
                    }
                    int ActiveIndex = Frame->Order[J];
                    int X = (4096*(ActiveIndex % GridSize)) / GridSize;
                    int Y = (4096*(GridSize - (ActiveIndex / GridSize) - 1)) / GridSize;
                    fprintf(File, "{%d, %d}, ", X, Y);
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
