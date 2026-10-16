
#include <r3d/r3d.h>
#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

typedef bool (*TestFunction)(void);

typedef struct {
    const char* name;
    TestFunction function;
} TestCase;

typedef struct {
    int calls;
    char pass[64];
    char stage[32];
    char message[1024];
    bool hasVertexSource;
    bool hasFragmentSource;
    bool hasExpectedMarker;
} DiagnosticState;

static DiagnosticState diagnosticState;

static void on_shader_diagnostic(const char* pass, const char* stage, const char* message, const char* vertexSource, const char* fragmentSource, void* userData)
{
    DiagnosticState* state = (DiagnosticState*)userData;

    state->calls++;

    snprintf(state->pass, sizeof(state->pass), "%s", pass ? pass : "");
    snprintf(state->stage, sizeof(state->stage), "%s", stage ? stage : "");
    snprintf(state->message, sizeof(state->message), "%s", message ? message : "");

    state->hasVertexSource = vertexSource && vertexSource[0];
    state->hasFragmentSource = fragmentSource && fragmentSource[0];

    state->hasExpectedMarker =
        (vertexSource && strstr(vertexSource, "R3D_DIAG_INVALID_")) ||
        (fragmentSource && strstr(fragmentSource, "R3D_DIAG_INVALID_"));
}

static bool test_valid_shader(void)
{
    const char* source =
        "#pragma usage opaque\n"
        "void vertex() {\n"
        "    POSITION.y += 0.1;\n"
        "}\n"
        "void fragment() {\n"
        "    ALBEDO = vec3(1.0, 0.2, 0.4);\n"
        "}\n";

    R3D_SurfaceShader* shader = R3D_LoadSurfaceShaderFromMemory(source);

    CHECK(shader != NULL);
    CHECK(diagnosticState.calls == 0);

    R3D_UnloadSurfaceShader(shader);
    return true;
}

static bool test_invalid_vertex(void)
{
    const char* source =
        "#pragma usage opaque\n"
        "void vertex() {\n"
        "    POSITION.x += R3D_DIAG_INVALID_VERTEX_SYMBOL;\n"
        "}\n";

    R3D_SurfaceShader* shader = R3D_LoadSurfaceShaderFromMemory(source);

    if (shader) R3D_UnloadSurfaceShader(shader);

    CHECK(shader == NULL);
    CHECK(diagnosticState.calls == 1);
    CHECK(strcmp(diagnosticState.pass, "geometry") == 0);
    CHECK(strcmp(diagnosticState.stage, "vertex") == 0);
    CHECK(diagnosticState.message[0] != '\0');
    CHECK(diagnosticState.hasVertexSource);
    CHECK(diagnosticState.hasFragmentSource);
    CHECK(diagnosticState.hasExpectedMarker);

    return true;
}

static bool test_invalid_fragment(void)
{
    const char* source =
        "#pragma usage opaque\n"
        "void fragment() {\n"
        "    ALBEDO = vec3(R3D_DIAG_INVALID_FRAGMENT_SYMBOL);\n"
        "}\n";

    R3D_SurfaceShader* shader = R3D_LoadSurfaceShaderFromMemory(source);

    if (shader) R3D_UnloadSurfaceShader(shader);

    CHECK(shader == NULL);
    CHECK(diagnosticState.calls == 1);
    CHECK(strcmp(diagnosticState.pass, "geometry") == 0);
    CHECK(strcmp(diagnosticState.stage, "fragment") == 0);
    CHECK(diagnosticState.message[0] != '\0');
    CHECK(diagnosticState.hasVertexSource);
    CHECK(diagnosticState.hasFragmentSource);
    CHECK(diagnosticState.hasExpectedMarker);

    return true;
}

int main(int argc, char** argv)
{
    const TestCase tests[] = {
        {"valid", test_valid_shader},
        {"invalid_vertex", test_invalid_vertex},
        {"invalid_fragment", test_invalid_fragment}
    };

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <test-name>\n", argv[0]);
        return 2;
    }

    const TestCase* selected = NULL;

    for (int i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    {
        if (strcmp(argv[1], tests[i].name) == 0)
        {
            selected = &tests[i];
            break;
        }
    }

    if (!selected)
    {
        fprintf(stderr, "Unknown shader test: %s\n", argv[1]);
        return 2;
    }

    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 180, "R3D Shader Diagnostics Test");

    if (!IsWindowReady())
    {
        fprintf(stderr, "FAIL: Could not create OpenGL context\n");
        CloseWindow();
        return 1;
    }

    if (!R3D_Init(64, 64))
    {
        fprintf(stderr, "FAIL: R3D initialization failed\n");
        CloseWindow();
        return 1;
    }

    memset(&diagnosticState, 0, sizeof(diagnosticState));

    R3D_SetShaderDiagnosticCallback(
        on_shader_diagnostic,
        &diagnosticState
    );

    bool passed = selected->function();

    R3D_SetShaderDiagnosticCallback(NULL, NULL);

    R3D_Close();
    CloseWindow();

    printf("%s: %s\n", selected->name, passed ? "PASS" : "FAIL");

    return passed ? 0 : 1;
}
