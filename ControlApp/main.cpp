#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include <gl3w.c>
#include <glfw/include/GLFW/glfw3.h>

#include <stdio.h>

static void glfwErrorCallback(int Error, const char* Description) {
    fprintf(stderr, "GLFW Error %d: %s\n", Error, Description);
}

enum {
    GridSize = 64,
    MaxActive = 300,
    MaxFrames = 10
};

typedef struct {
    int ActiveCount;
    int Order[MaxActive];
    bool Active[GridSize*GridSize];
} frame;

int FrameCount = 1;
int SelectedFrame = 0;
frame Frames[MaxFrames] = {0};

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
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Projeto de Computadores", NULL, NULL);
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

    ImVec4 ClearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        frame *Frame = Frames + SelectedFrame;

        ImGui::Begin("Grelha", 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        for(int I = 0; I < GridSize*GridSize; I++) {
            ImGui::PushID(I);
            if(ImGui::Selectable("", Frame->Active[I], 0, ImVec2(5, 10))) {
                if(!Frame->Active[I] && Frame->ActiveCount < MaxActive) {
                    Frame->Order[Frame->ActiveCount++] = I;
                    Frame->Active[I] = true;
                }
            }
            if((I % GridSize) < GridSize-1) {
                ImGui::SameLine();
            }
            ImGui::PopID();
        }
        ImGui::End();

        ImGui::Begin("Comandos", 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SliderInt("Frame count", &FrameCount, 1, MaxFrames);
        if(SelectedFrame >= FrameCount) {
            SelectedFrame = FrameCount-1;
        }
        ImGui::SliderInt("Selected frame", &SelectedFrame, 0, FrameCount-1);
        if(ImGui::Button("Exportar para C")) {
            FILE *File = fopen("generated_header.h", "w");
            fprintf(File, "point FramePoints[] = {\n    ");
            for(int I = 0; I < Frame->ActiveCount; ++I) {
                if(((I+1) % 7) == 0) {
                    fprintf(File, "\n    ");
                }
                int ActiveIndex = Frame->Order[I];
                int X = (4096*(ActiveIndex % GridSize)) / GridSize;
                int Y = (4096*(GridSize - (ActiveIndex / GridSize) - 1)) / GridSize;
                fprintf(File, "{%d, %d}, ", X, Y);
            }
            fprintf(File, "\n};\n");
            fclose(File);
        }

        if(ImGui::Button("Apagar frame")) {
            Frame->ActiveCount = 0;
            for(int I = 0; I < GridSize*GridSize; ++I) {
                Frame->Active[I] = false;
            }
        }
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwMakeContextCurrent(window);
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(ClearColor.x, ClearColor.y, ClearColor.z, ClearColor.w);
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
