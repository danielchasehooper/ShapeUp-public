#include <assert.h>
#include <stdarg.h>
#include "stdint.h"
#include "string.h"
#include "inttypes.h"
#include <float.h> 
#include "shaders.h"

#define DEMO_VIDEO_FEATURES 0

#ifdef PLATFORM_WEB
#include <emscripten.h>
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

Vector3 GetCameraRight(Camera *camera);
Vector3 GetCameraForward(Camera *camera);
void CameraMoveForward(Camera *camera, float distance, bool moveInWorldPlane);
void CameraMoveUp(Camera *camera, float distance);
void CameraMoveRight(Camera *camera, float distance, bool moveInWorldPlane);

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "dark.h"

#pragma clang diagnostic pop

#define print(...) TraceLog(LOG_ERROR, __VA_ARGS__)

const int sidebar_width = 210;

typedef enum {
    CONTROL_NONE,
    CONTROL_POS_X,
    CONTROL_POS_Y,
    CONTROL_POS_Z,
    CONTROL_SCALE_X,
    CONTROL_SCALE_Y,
    CONTROL_SCALE_Z,
    CONTROL_ANGLE_X,
    CONTROL_ANGLE_Y,
    CONTROL_ANGLE_Z,
    CONTROL_COLOR_R,
    CONTROL_COLOR_G,
    CONTROL_COLOR_B,
    CONTROL_TRANSLATE,
    CONTROL_ROTATE,
    CONTROL_SCALE,
    CONTROL_CORNER_RADIUS,
    CONTROL_ROTATE_CAMERA,
    CONTROL_BLOB_AMOUNT,
} Control;

Control focusedControl;
Control mouseAction;

Vector2 scroll_offset;

union {
    struct {
        bool x : 1;
        bool y : 1;
        bool z : 1;
    };
    int mask;
} controlled_axis = {.mask = 0x7};
double last_axis_set = 0;

typedef struct {
    Vector3 pos;
    Vector3 size;
    Vector3 angle;
    float corner_radius;
    float blob_amount;
    struct {
        uint8_t r,g,b;
    } color;
    struct {
        bool x,y,z;
    } mirror;
    bool subtract;
} Sphere;

bool needs_rebuild = true;

#if DEMO_VIDEO_FEATURES
bool false_color_mode = false;
#endif

enum {
    VISUALS_NONE,
    VISUALS_SDF,
} visuals_mode;

Shader main_shader;
struct {
    int viewEye;
    int viewCenter;
    int runTime;
    int resolution;
    int selectedParams;
    int visualizer;
} main_locations;

int num_spheres = 1;
#define MAX_SPHERES 100
Sphere spheres[MAX_SPHERES];
int selected_sphere = 0;

Camera camera = { 0 };

Color last_color_set = {
    (uint8_t)(0.941*255),
    (uint8_t)(0.631*255),
    (uint8_t)(0.361*255),
    0
};

double lastSave;

void append(char **str1, const char *str2){
    assert(str1);
    assert(str2);
    size_t len = (*str1 ? strlen(*str1) : 0) + strlen(str2);

    char *concatenated = (char *)malloc(len + 1);
    concatenated[0] = 0;
    assert(concatenated);

    if (*str1) {
        strcpy(concatenated, *str1);
        free(*str1);
    }
    strcat(concatenated, str2);

    *str1 = concatenated;
}

int RayPlaneIntersection(const Vector3 RayOrigin, const Vector3 RayDirection, const Vector3 PlanePoint, const Vector3 PlaneNormal, Vector3 *IntersectionPoint) {
    float dotProduct = (PlaneNormal.x * RayDirection.x) + (PlaneNormal.y * RayDirection.y) + (PlaneNormal.z * RayDirection.z);

    // Check if the ray is parallel to the plane
    if (dotProduct == 0.0f) {
        return 0;
    }

    float t = ((PlanePoint.x - RayOrigin.x) * PlaneNormal.x + (PlanePoint.y - RayOrigin.y) * PlaneNormal.y + (PlanePoint.z - RayOrigin.z) * PlaneNormal.z) / dotProduct;

    IntersectionPoint->x = RayOrigin.x + t * RayDirection.x;
    IntersectionPoint->y = RayOrigin.y + t * RayDirection.y;
    IntersectionPoint->z = RayOrigin.z + t * RayDirection.z;

    return 1;
}

Vector3 WorldToCamera(Vector3 worldPos, Matrix cameraMatrix) {
    return  Vector3Transform(worldPos, cameraMatrix);
}

Vector3 CameraToWorld(Vector3 worldPos, Matrix cameraMatrix) {
    return Vector3Transform(worldPos, MatrixInvert(cameraMatrix));
}

Vector3 VectorProjection(const Vector3 vectorToProject, const Vector3 targetVector) {
    float dotProduct = (vectorToProject.x * targetVector.x) +
                      (vectorToProject.y * targetVector.y) +
                      (vectorToProject.z * targetVector.z);
    
    float targetVectorLengthSquared = (targetVector.x * targetVector.x) +
                                      (targetVector.y * targetVector.y) +
                                      (targetVector.z * targetVector.z);
    
    float scale = dotProduct / targetVectorLengthSquared;

    Vector3 projection;
    projection.x = targetVector.x * scale;
    projection.y = targetVector.y * scale;
    projection.z = targetVector.z * scale;

    return projection;
}

// Find the point on line p1 to p2 nearest to line p2 to p4
Vector3 NearestPointOnLine(Vector3 p1,
                           Vector3 p2,
                           Vector3 p3,
                           Vector3 p4)
{
    float mua;

    Vector3 p13,p43,p21;
    float d1343,d4321,d1321,d4343,d2121;
    float numer,denom;

    const float EPS = 0.001;

    p13.x = p1.x - p3.x;
    p13.y = p1.y - p3.y;
    p13.z = p1.z - p3.z;
    p43.x = p4.x - p3.x;
    p43.y = p4.y - p3.y;
    p43.z = p4.z - p3.z;
    if (fabs(p43.x) < EPS && fabs(p43.y) < EPS && fabs(p43.z) < EPS)
        return(Vector3){};
    p21.x = p2.x - p1.x;
    p21.y = p2.y - p1.y;
    p21.z = p2.z - p1.z;
    if (fabs(p21.x) < EPS && fabs(p21.y) < EPS && fabs(p21.z) < EPS)
        return(Vector3){};

    d1343 = p13.x * p43.x + p13.y * p43.y + p13.z * p43.z;
    d4321 = p43.x * p21.x + p43.y * p21.y + p43.z * p21.z;
    d1321 = p13.x * p21.x + p13.y * p21.y + p13.z * p21.z;
    d4343 = p43.x * p43.x + p43.y * p43.y + p43.z * p43.z;
    d2121 = p21.x * p21.x + p21.y * p21.y + p21.z * p21.z;

    denom = d2121 * d4343 - d4321 * d4321;
    if (fabs(denom) < EPS)
        return (Vector3){};
    numer = d1343 * d4321 - d1321 * d4343;

    mua = numer / denom;

    return (Vector3){ 
        p1.x + mua * p21.x,
        p1.y + mua * p21.y,
        p1.z + mua * p21.z};

}

BoundingBox boundingBoxSized(Vector3 center, float size) {
    return (BoundingBox){
        Vector3SubtractValue(center, size/2),
        Vector3AddValue(center, size/2),
    };
}

BoundingBox shapeBoundingBox(Sphere s) {
    // const float radius = sqrtf(powf(s.size.x, 2) + powf(s.size.y, 2) + powf(s.size.z, 2));
    return (BoundingBox){
        Vector3Subtract(s.pos, s.size),
        Vector3Add(s.pos, s.size),
    };
}

int GuiFloatValueBox(Rectangle bounds, const char *text, float *value, float minValue, float maxValue, bool editMode)
{
    #if !defined(RAYGUI_VALUEBOX_MAX_CHARS)
        #define RAYGUI_VALUEBOX_MAX_CHARS  32
    #endif

    int result = 0;
    GuiState state = guiState;

    static char editingTextValue[RAYGUI_VALUEBOX_MAX_CHARS + 1] = "\0";
    char textValue[RAYGUI_VALUEBOX_MAX_CHARS + 1] = "\0";
    static float original_value;
    static int key_delay = 0;
    snprintf(textValue, RAYGUI_VALUEBOX_MAX_CHARS, "%g", *value);

    Rectangle textBounds = { 0 };
    if (text != NULL)
    {
        textBounds.width = (float)(GetTextWidth(text) + 2);
        textBounds.height = (float)(GuiGetStyle(DEFAULT, TEXT_SIZE));
        textBounds.x = bounds.x + bounds.width + GuiGetStyle(VALUEBOX, TEXT_PADDING);
        textBounds.y = bounds.y + bounds.height/2 - GuiGetStyle(DEFAULT, TEXT_SIZE)/2;
        if (GuiGetStyle(VALUEBOX, TEXT_ALIGNMENT) == TEXT_ALIGN_LEFT) textBounds.x = bounds.x - textBounds.width - GuiGetStyle(VALUEBOX, TEXT_PADDING);
    }

    char *text_to_display = textValue;

    // Update control
    //--------------------------------------------------------------------
    if ((state != STATE_DISABLED) && !guiLocked && !guiSliderDragging)
    {
        Vector2 mousePoint = GetMousePosition();

        bool valueHasChanged = false;

        if (editMode)
        {   
            state = STATE_PRESSED;

            int keyCount = (int)strlen(editingTextValue);

            // Only allow keys in range [48..57]
            if (keyCount < RAYGUI_VALUEBOX_MAX_CHARS)
            {
                int key = GetCharPressed();
                if (((key >= 48) && (key <= 57)) || key == 46)
                {
                    editingTextValue[keyCount] = (char)key;
                    keyCount++;
                    valueHasChanged = true;
                }
            }

            // Delete text
            if (keyCount > 0)
            {
                if (IsKeyPressed(KEY_BACKSPACE))
                {
                    keyCount--;
                    editingTextValue[keyCount] = '\0';
                    valueHasChanged = true;
                }
            }

            if (!valueHasChanged && IsKeyDown(KEY_UP) && key_delay <= 0) {
                *value+=fabs(*value*0.1) + 0.1;
                if (*value > maxValue) *value = maxValue;
                key_delay=9;
                sprintf(editingTextValue, "%g", *value);
            }else if (!valueHasChanged && IsKeyDown(KEY_DOWN) && key_delay <= 0) {
                *value-=fabs(*value*0.1) + 0.1;
                if (*value < minValue) *value = minValue;
                key_delay=9;
                sprintf(editingTextValue, "%g", *value);
            }

            if (key_delay > 0) key_delay--;
            if (!IsKeyDown(KEY_UP) && !IsKeyDown(KEY_DOWN))key_delay = 0;
            text_to_display = editingTextValue;
            if (IsKeyPressed(KEY_ESCAPE)) {
                *value = original_value;
                result = 1;
            } else {
                if (valueHasChanged) *value = (float)strtod(editingTextValue, NULL);
                if (IsKeyPressed(KEY_ENTER) || (!CheckCollisionPointRec(mousePoint, bounds) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))) result = 1;
            }
        }
        else
        {
            if (*value > maxValue) *value = maxValue;
            else if (*value < minValue) *value = minValue;

            if (CheckCollisionPointRec(mousePoint, bounds))
            {
                state = STATE_FOCUSED;
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    original_value = *value;
                    memcpy(editingTextValue, textValue, RAYGUI_VALUEBOX_MAX_CHARS + 1);
                    result = 1;
                }
            }
        }
    }
    //--------------------------------------------------------------------

    // Draw control
    //--------------------------------------------------------------------
    Color baseColor = BLANK;
    if (state == STATE_PRESSED) baseColor = GetColor(GuiGetStyle(VALUEBOX, BASE_COLOR_PRESSED));
    else if (state == STATE_DISABLED)  baseColor = GetColor(GuiGetStyle(VALUEBOX, BASE_COLOR_DISABLED));
    else if (state == STATE_FOCUSED) baseColor = WHITE;

    GuiDrawRectangle(bounds, 0, GetColor(GuiGetStyle(VALUEBOX, BORDER + (state*3))), baseColor);
    GuiDrawText(text_to_display, GetTextBounds(VALUEBOX, bounds), TEXT_ALIGN_LEFT, GetColor(GuiGetStyle(VALUEBOX, TEXT)));

    // Draw cursor
    if (editMode)
    {
        Rectangle cursor = { bounds.x + GetTextWidth(text_to_display) + 1, bounds.y + 2*GuiGetStyle(VALUEBOX, BORDER_WIDTH), 4, bounds.height - 4*GuiGetStyle(VALUEBOX, BORDER_WIDTH) };
        GuiDrawRectangle(cursor, 0, BLANK, GetColor(GuiGetStyle(LABEL, TEXT + (state*3))));
    }

    // Draw text label if provided
    GuiDrawText(text, textBounds, (GuiGetStyle(VALUEBOX, TEXT_ALIGNMENT) == TEXT_ALIGN_RIGHT)? TEXT_ALIGN_LEFT : TEXT_ALIGN_RIGHT, GetColor(GuiGetStyle(LABEL, TEXT)));
    //--------------------------------------------------------------------

    return result;
}

void append_map_function(char **result, bool use_color_as_index, int dynamic_index) {

    char *map = NULL;

    append(&map, "uniform vec3 selectionValues[5];\n\n");

    #if DEMO_VIDEO_FEATURES
    if (false_color_mode) {
        append(&map, "#define FALSE_COLOR_MODE 1\n\n");
    }
    #endif

    append(&map,
        "vec4 signed_distance_field( in vec3 pos ){\n"
        "\tvec4 distance = vec4(999999.,0,0,0);\n");

    for (int i=0; i < num_spheres; i++) {
        Sphere s = spheres[i];
        float used_radius = fmaxf(0.01,fminf(s.corner_radius, fminf(s.size.x,fminf(s.size.y, s.size.z))));

        char *symmetry[8] = {
        "",
        "opSymX",
        "opSymY",
        "opSymXY", 
        "opSymZ",
        "opSymXZ",
        "opSymYZ",
        "opSymXYZ"};

        int mirror_index = (s.mirror.z << 2) | (s.mirror.y << 1) | s.mirror.x;
        if (i == dynamic_index) {
            append(&map, TextFormat("\tdistance = %s(\n"
                              "\t\tvec4(RoundBox(\n"
                              "\t\t\t\topRotateXYZ(\n"
                              "\t\t\t\t\t%s(pos) - selectionValues[0], // position\n"
                              "\t\t\t\t\tselectionValues[1]), // angle\n"
                              "\t\t\t\tselectionValues[2],  // size\n"
                              "\t\t\t\tselectionValues[4].x), // corner radius\n"
                              "\t\t\tselectionValues[3]), // color\n"
                              "\t\tdistance,\n\t\tselectionValues[4].y); // blobbyness\n",
                              s.subtract ? "opSmoothSubtraction" : "BlobbyMin",
                              symmetry[mirror_index]));
        } else {

            uint8_t r = use_color_as_index ?  (uint8_t)(i+1) : s.color.r;
            uint8_t g = use_color_as_index ?  0 : s.color.g;
            uint8_t b = use_color_as_index ?  0 : s.color.b;

#           if DEMO_VIDEO_FEATURES
            if (false_color_mode) {
                Color c = ColorFromHSV((i*97) % 360, 1, 0.5);
                r = c.r;
                g = c.g;
                b = c.b;
            }
#           endif


            char *opName = s.subtract ? "opSmoothSubtraction" : ((use_color_as_index
                                                                #if DEMO_VIDEO_FEATURES
                                                                  || false_color_mode
                                                                #endif
                                                                  ) ? "opSmoothUnionSteppedColor" : (s.blob_amount > 0 ? "BlobbyMin" : "Min"));
            append(&map,  TextFormat("\tdistance = %s(\n\t\tvec4(RoundBox(\n", opName));

            const bool rotated = fabsf(s.angle.x) > .01 || fabsf(s.angle.y) > 0.01 || fabsf(s.angle.z) > .01;
            if (rotated) {
                    float cz = cos(s.angle.z);
                    float sz = sin(s.angle.z);
                    float cy = cos(s.angle.y);
                    float sy = sin(s.angle.y);
                    float cx = cos(s.angle.x);
                    float sx = sin(s.angle.x);

                    append(&map, TextFormat("\t\t\tmat3(%g, %g, %g,"
                                            "%g, %g, %g,"
                                            "%g, %g, %g)*\n",
                                            cz*cy,
                                            cz*sy*sx - cx*sz,
                                            sz*sx + cz*cx*sy,

                                            cy*sz,
                                            cz*cx + sz*sy*sx,
                                            cx*sz*sy - cz*sx,

                                            -sy,
                                            cy*sx,
                                            cy*cx
                                            ));
            }

            append(&map, TextFormat("\t\t\t\t(%s(pos) - vec3(%g,%g,%g)), // position\n",
                                  symmetry[mirror_index],
                                  s.pos.x, s.pos.y, s.pos.z));



            append(&map, TextFormat("\t\t\tvec3(%g,%g,%g),// size\n"
                              "\t\t\t%g), // corner radius\n"
                              "\t\t\t%g,%g,%g), // color\n"
                              "\t\tdistance",
                              s.size.x-used_radius, s.size.y-used_radius, s.size.z-used_radius,
                              used_radius,
                              r/255.f, g/255.f, b/255.f));

            if (!strcmp(opName, "Min")) {
                append(&map,  TextFormat(");\n"));
            } else {
                append(&map,  TextFormat(",\n\t\t%g);  // blobbyness\n", fmaxf(s.blob_amount, 0.0001)));
            }

        }


    }

    append(&map, "\treturn distance;\n}\n");

#   if DEMO_VIDEO_FEATURES
    SaveFileText("map.glsl", map);
#   endif

    append(result, map);
    free(map);
}

#ifdef PLATFORM_WEB
#define SHADER_VERSION_PREFIX "#version 300 es\nprecision highp float;"
#else
#define SHADER_VERSION_PREFIX "#version 330\n" 
#endif

const char *vshader =     
    SHADER_VERSION_PREFIX
    "in vec3 vertexPosition;            \n"
    "in vec2 vertexTexCoord;            \n"
    "out vec2 fragTexCoord;             \n"
    "uniform mat4 mvp;                  \n"
    "void main()                        \n"
    "{                                  \n"
    "    fragTexCoord = vertexTexCoord; \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                  \n";

void rebuild_shaders(void ) {
    needs_rebuild = false;
    UnloadShader(main_shader); 

    char *map_function = NULL;
    append_map_function(&map_function, false, selected_sphere);

    char *result = NULL;
    append(&result, 
        SHADER_VERSION_PREFIX
           "out vec4 finalColor; \
           uniform vec3 viewEye; \
           uniform vec3 viewCenter; \
           uniform float runTime; \
           uniform float visualizer; \
           uniform vec2 resolution;");
    append(&result, shader_prefix_fs);
    append(&result, map_function);
    append(&result, shader_base_fs);
    main_shader = LoadShaderFromMemory(vshader, (char *)result);
    free(result);
    result = NULL;

    main_locations.viewEye = GetShaderLocation(main_shader, "viewEye");
    main_locations.viewCenter = GetShaderLocation(main_shader, "viewCenter");
    main_locations.runTime = GetShaderLocation(main_shader, "runTime");
    main_locations.resolution = GetShaderLocation(main_shader, "resolution");
    main_locations.selectedParams = GetShaderLocation(main_shader, "selectionValues");
    main_locations.visualizer = GetShaderLocation(main_shader, "visualizer");


    free(map_function);
}

void delete_sphere(int index) {
    memmove(&spheres[index], &spheres[index+1], sizeof(Sphere)*(num_spheres-index));
    num_spheres--;

    if (selected_sphere == index) {
        selected_sphere = num_spheres-1;
    } else if (selected_sphere > index) {
        selected_sphere--;
    }

    needs_rebuild = true;
}

void add_shape(void) {
    if (num_spheres >= MAX_SPHERES) return;

    spheres[num_spheres] = (Sphere){
        .size = { 1, 1, 1 },
        .color = {
            last_color_set.r,
            last_color_set.g,
            last_color_set.b,
        },
    };
    selected_sphere = num_spheres;
    num_spheres++;
    needs_rebuild = true;
}

#define MIN(x,y) ({ \
        __typeof(x) xv = (x);\
        __typeof(y) yv = (y); \
        xv < yv ? xv : yv;\
    })

__attribute__((format(printf, 4, 5)))
void append_format(char **data, int *size, int *capacity, const char *format, ...) {

    va_list arg_ptr;
    va_start(arg_ptr, format);
    int added = vsnprintf(*data+*size, *capacity-*size,format, arg_ptr); 

    *size+=MIN(added, *capacity - *size);
    assert(*size < *capacity);

    va_end(arg_ptr);
}

Vector3 VertexInterp(Vector4 p1,Vector4 p2, float threshold) {

   if (fabsf(threshold-p1.w) < 0.00001)
      return *(Vector3 *)&p1;
   if (fabsf(threshold-p2.w) < 0.00001)
      return *(Vector3 *)&p2;
   if (fabsf(p1.w-p2.w) < 0.00001)
      return *(Vector3 *)&p1;

   float mu = (threshold - p1.w) / (p2.w - p1.w);
   Vector3 r = {
    p1.x + mu * (p2.x - p1.x),
    p1.y + mu * (p2.y - p1.y),
    p1.z + mu * (p2.z - p1.z),
   };

   return r;
}

uint64_t FNV1a_64_hash(uint8_t *data, int len) {
    uint64_t hash = 0xcbf29ce484222325;
    for (int i=0; i < len; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3;
    }

    return hash;
}

void save(char *name) {
    const int size = sizeof(int) + sizeof(Sphere)*num_spheres;
    char *data = malloc(size);
    *(int *)(void *)data = num_spheres;
    memcpy(data + sizeof(int), spheres, num_spheres*sizeof(Sphere));

    SaveFileData(TextFormat("build/%s_%"PRIu64".shapeup", name, FNV1a_64_hash(data, size)), data, size);

    free(data);
    lastSave = GetTime();
}

void openSnapshot(char *path) {
    print("opening ========= %s", path);

    unsigned int size;
    unsigned char *data = LoadFileData(path, &size);

    assert(data);

    num_spheres = *(int *)(void *)data;
    memcpy(spheres, data + sizeof(int), sizeof(Sphere)*num_spheres);

    UnloadFileData(data);

    selected_sphere = -1;
    needs_rebuild = true;
    lastSave = GetTime();
}

RenderTexture2D LoadFloatRenderTexture(int width, int height)
{
    RenderTexture2D target = { 0 };

    target.id = rlLoadFramebuffer(width, height);   // Load an empty framebuffer

    if (target.id > 0)
    {
        rlEnableFramebuffer(target.id);

        target.texture.format = PIXELFORMAT_UNCOMPRESSED_R32;
        target.texture.id = rlLoadTexture(NULL, width, height, target.texture.format, 1);
        target.texture.width = width;
        target.texture.height = height;
        target.texture.mipmaps = 1;

        // Attach color texture and depth renderbuffer/texture to FBO
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

        if (rlFramebufferComplete(target.id)) {
            print("FBO: [ID %i] Framebuffer object created successfully", target.id);
        }
        else {
            print("FBO: [ID %i] Framebuffer object FAILED", target.id);
        }

        rlDisableFramebuffer();
    }
    else TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");

    return target;
}

// from https://paulbourke.net/geometry/polygonise/
const int edgeTable[256]={
    0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
    0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
    0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
    0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
    0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
    0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
    0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
    0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
    0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
    0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
    0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
    0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
    0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
    0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
    0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
    0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
    0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0   };
    const int triTable[256][16] =
    {{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
    {3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1},
    {4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
    {9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
    {10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
    {5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
    {8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
    {2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
    {11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
    {5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
    {11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
    {11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
    {9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
    {6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
    {6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
    {8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
    {7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1},
    {3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
    {9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
    {8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
    {0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
    {6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
    {10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
    {10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
    {0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
    {3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
    {9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1},
    {8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1},
    {3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
    {10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
    {10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1},
    {7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
    {1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
    {11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
    {8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
    {0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
    {7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1},
    {7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1},
    {2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1},
    {10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1},
    {0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1},
    {7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1},
    {9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1},
    {6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1},
    {4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1},
    {10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1},
    {8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1},
    {0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1},
    {1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1},
    {10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1},
    {10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1},
    {9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1},
    {7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1},
    {3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1},
    {7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1},
    {3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1},
    {6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1},
    {9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1},
    {1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1},
    {4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1},
    {7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1},
    {6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1},
    {3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
    {0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1},
    {6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1},
    {0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1},
    {11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1},
    {6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1},
    {5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1},
    {9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1},
    {1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1},
    {10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1},
    {0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1},
    {5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1},
    {10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1},
    {11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1},
    {9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1},
    {7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1},
    {2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1},
    {8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1},
    {9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1},
    {1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1},
    {10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1},
    {2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1},
    {0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1},
    {0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1},
    {9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1},
    {5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1},
    {5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1},
    {8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1},
    {0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1},
    {9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1},
    {1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
    {3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1},
    {4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1},
    {9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1},
    {11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1},
    {2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1},
    {9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1},
    {3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1},
    {1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1},
    {4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1},
    {0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1},
    {1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};



void export(void) {

    char *shader_source = NULL;
    append(&shader_source, SHADER_VERSION_PREFIX);
    append(&shader_source, shader_prefix_fs);
    append_map_function(&shader_source, false, -1);
    append(&shader_source, slicer_body_fs);

    Shader slicer_shader = LoadShaderFromMemory(vshader, (char *)shader_source);
    free(shader_source);
    shader_source = NULL;

    int slicer_z_loc = GetShaderLocation(slicer_shader, "z");

    double startTime = GetTime();
    const float cube_resolution = 0.03;

    BoundingBox bounds = {
        {FLT_MAX,FLT_MAX,FLT_MAX},
        {-FLT_MAX,-FLT_MAX,-FLT_MAX}
    };

    for(int i=0; i < num_spheres; i++) {
        const float radius = sqrtf(powf(spheres[i].size.x, 2) + powf(spheres[i].size.y, 2) + powf(spheres[i].size.z, 2));
        bounds.min.x = fminf(bounds.min.x, spheres[i].pos.x - radius); 
        bounds.min.y = fminf(bounds.min.y, spheres[i].pos.y - radius);
        bounds.min.z = fminf(bounds.min.z, spheres[i].pos.z - radius);

        bounds.max.x = fmaxf(bounds.max.x, spheres[i].pos.x + radius); 
        bounds.max.y = fmaxf(bounds.max.y, spheres[i].pos.y + radius);
        bounds.max.z = fmaxf(bounds.max.z, spheres[i].pos.z + radius);
    }

    // the marching cube sampling lattace must extend beyond the objects you want it to represent
    bounds.min.x -= 1;
    bounds.min.y -= 1;
    bounds.min.z -= 1;

    bounds.max.x += 1;
    bounds.max.y += 1;
    bounds.max.z += 1;

    const int slice_count_x = (int)((bounds.max.x - bounds.min.x) / cube_resolution + 1.5);
    const int slice_count_y = (int)((bounds.max.y - bounds.min.y) / cube_resolution + 1.5);
    const int slice_count_z = (int)((bounds.max.z - bounds.min.z) / cube_resolution + 1.5);

    const float x_step = (bounds.max.x - bounds.min.x) / (slice_count_x-1);
    const float y_step = (bounds.max.y - bounds.min.y) / (slice_count_y-1);
    const float z_step = (bounds.max.z - bounds.min.z) / (slice_count_z-1);

    int data_capacity = 400000000;
    char *data = malloc(data_capacity);
    int data_size = 0;

    RenderTexture2D sliceTexture[2];
    sliceTexture[0] = LoadFloatRenderTexture(slice_count_x, slice_count_y);
    sliceTexture[1] = LoadFloatRenderTexture(slice_count_x, slice_count_y);
    
    for (int z_index = 0; z_index < slice_count_z-1; z_index++) {
        for (int side =0; side < 2; side++) {
            float z = bounds.min.z + (z_index+side)*z_step;
            SetShaderValue(slicer_shader, slicer_z_loc, &z, SHADER_UNIFORM_FLOAT);
            BeginTextureMode(sliceTexture[side]); {
                BeginShaderMode(slicer_shader); {
                    rlBegin(RL_QUADS);
                    rlTexCoord2f(bounds.max.x, bounds.min.y);
                    rlVertex2f(0, 0);

                    rlTexCoord2f(bounds.max.x, bounds.max.y);
                    rlVertex2f(0, slice_count_y);

                    rlTexCoord2f(bounds.min.x, bounds.max.y);
                    rlVertex2f(slice_count_x, slice_count_y);

                    rlTexCoord2f(bounds.min.x, bounds.min.y);
                    rlVertex2f(slice_count_x, 0);
                    rlEnd();
                } EndShaderMode();
            } EndTextureMode();
        }

        float *pixels = rlReadTexturePixels(sliceTexture[0].texture.id, sliceTexture[0].texture.width, sliceTexture[0].texture.height, sliceTexture[0].texture.format);
        float *pixels2 = rlReadTexturePixels(sliceTexture[1].texture.id, sliceTexture[1].texture.width, sliceTexture[1].texture.height, sliceTexture[1].texture.format);

        #define SDF_THRESHOLD (0)
        for (int y_index=0; y_index < slice_count_y-1; y_index++) { 
            for (int x_index=0; x_index < slice_count_x-1; x_index++) {

                float val0 = pixels [(x_index  + y_index   *slice_count_x)*1] ;
                float val1 = pixels [(x_index+1+ y_index   *slice_count_x)*1] ;
                float val2 = pixels [(x_index+1 +(y_index+1)*slice_count_x)*1];
                float val3 = pixels [(x_index   +(y_index+1)*slice_count_x)*1];
                float val4 = pixels2[(x_index  + y_index   *slice_count_x)*1] ;
                float val5 = pixels2[(x_index+1+ y_index   *slice_count_x)*1] ;
                float val6 = pixels2[(x_index+1+(y_index+1)*slice_count_x)*1] ;
                float val7 = pixels2[(x_index  +(y_index+1)*slice_count_x)*1] ;

                Vector4 v0 = {
                    bounds.min.x + x_index*x_step,
                    bounds.min.y + y_index*y_step,
                    bounds.min.z + z_index*z_step,
                    val0
                };
                Vector4 v1 = {v0.x+x_step,  v0.y,        v0.z, val1};
                Vector4 v2 = {v0.x+x_step,  v0.y+y_step, v0.z, val2};
                Vector4 v3 = {v0.x,         v0.y+y_step, v0.z, val3};

                Vector4 v4 = {v0.x,         v0.y,        v0.z+z_step, val4};
                Vector4 v5 = {v0.x+x_step,  v0.y,        v0.z+z_step, val5};
                Vector4 v6 = {v0.x+x_step,  v0.y+y_step, v0.z+z_step, val6};
                Vector4 v7 = {v0.x,         v0.y+y_step, v0.z+z_step, val7};

                int cubeindex = (val0 < SDF_THRESHOLD) << 0 | 
                            (val1 < SDF_THRESHOLD) << 1 |
                            (val2 < SDF_THRESHOLD) << 2 |
                            (val3 < SDF_THRESHOLD) << 3 |
                            (val4 < SDF_THRESHOLD) << 4 |
                            (val5 < SDF_THRESHOLD) << 5 |
                            (val6 < SDF_THRESHOLD) << 6 |
                            (val7 < SDF_THRESHOLD) << 7;

                /* Cube is entirely in/out of the surface */
                if (edgeTable[cubeindex] == 0) continue;

                Vector3 vertlist[12];
                if (edgeTable[cubeindex] & 1) vertlist[0] = VertexInterp(v0,v1, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 2) vertlist[1] = VertexInterp(v1,v2, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 4) vertlist[2] = VertexInterp(v2,v3, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 8) vertlist[3] = VertexInterp(v3,v0, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 16) vertlist[4] = VertexInterp(v4,v5, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 32) vertlist[5] = VertexInterp(v5,v6, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 64) vertlist[6] = VertexInterp(v6,v7, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 128) vertlist[7] = VertexInterp(v7,v4, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 256) vertlist[8] = VertexInterp(v0,v4, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 512) vertlist[9] = VertexInterp(v1,v5, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 1024) vertlist[10] = VertexInterp(v2,v6, SDF_THRESHOLD);
                if (edgeTable[cubeindex] & 2048) vertlist[11] = VertexInterp(v3,v7, SDF_THRESHOLD);

                for (int i=0;triTable[cubeindex][i]!=-1;i+=3) {
                    for (int v=0; v < 3; v++) {
                        Vector3 pt = vertlist[triTable[cubeindex][i + v]];
                        append_format(&data, &data_size, &data_capacity,"v %g %g %g\n", pt.x, -pt.y, pt.z); 
                    }

                    append_format(&data, &data_size, &data_capacity,"f -2  -1 -3\n"); 
                }

            }
        }

        free(pixels);
        free(pixels2);
    }

    UnloadRenderTexture(sliceTexture[0]);
    UnloadRenderTexture(sliceTexture[1]);


    SaveFileData("build/export.obj", data, data_size);

    free(data);

    UnloadShader(slicer_shader);

    double duration = GetTime() - startTime;
    print("export time %gms. size: %.2fMB", duration*1000, data_size/1000000.f);
}

int object_at_pixel(int x, int y) {
    const float start = GetTime();
    char *shader_source = NULL;
    append(&shader_source, SHADER_VERSION_PREFIX);
    append(&shader_source, shader_prefix_fs);
    append_map_function(&shader_source, true, -1);
    append(&shader_source, selection_fs);

    Shader shader = LoadShaderFromMemory(vshader, (char *)shader_source);
    free(shader_source);
    shader_source = NULL;

    int eye_loc = GetShaderLocation(shader, "viewEye");
    int center_loc = GetShaderLocation(shader, "viewCenter");
    int resolution_loc = GetShaderLocation(shader, "resolution");

    SetShaderValue(shader, eye_loc, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, center_loc, &camera.target, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, resolution_loc, (float[2]){ (float)GetScreenWidth(), (float)GetScreenHeight() }, SHADER_UNIFORM_VEC2);

    RenderTexture2D target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    BeginTextureMode(target); {
        BeginShaderMode(shader); {
            rlBegin(RL_QUADS);
            rlTexCoord2f(x-1, y-1);
            rlVertex2f(x-1, y-1);

            rlTexCoord2f(x-1, y+1);
            rlVertex2f(x-1, y+1);

            rlTexCoord2f(x+1, y+1);
            rlVertex2f(x+1, y+1);

            rlTexCoord2f(x+1, y-1);
            rlVertex2f(x+1, y-1);
            rlEnd();
        } EndShaderMode();
    } EndTextureMode();

    uint8_t *pixels = rlReadTexturePixels(target.texture.id, target.texture.width, target.texture.height, target.texture.format);

    int object_index = ((int)pixels[(x + target.texture.width*(target.texture.height-y))*4]) - 1;

    free(pixels);

    UnloadRenderTexture(target);
    UnloadShader(shader);

    print("picking object took %ims", (int)((-start + GetTime())*1000));

    return object_index;
}

int main(void){
    // SetTraceLogLevel(LOG_ERROR);
    lastSave = GetTime();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1940/2, 1100/2, "ShapeUp!");
    SetExitKey(0);

    GuiLoadStyleDark();
    // Font gfont = GuiGetFont();
    // GenTextureMipmaps(&guiFont.texture);
    // SetTextureFilter(guiFont.texture, TEXTURE_FILTER_POINT);
    // GuiSetFont(gfont);
    GuiSetStyle(DEFAULT, BORDER_WIDTH, 1);

    camera.position = (Vector3){ 2.5f, 2.5f, 3.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };  
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };      
    camera.fovy = 55.0f;                            
    camera.projection = CAMERA_PERSPECTIVE;

    spheres[0] = (Sphere){
        .size = {1,1,1}, 
    .color={
        last_color_set.r,
            last_color_set.g,
            last_color_set.b
        }};

    float runTime = 0.0f;

    SetTargetFPS(60);

#   ifndef PLATFORM_WEB
    void swizzleWindow(void);
    swizzleWindow();
    void makeWindowKey(void);
    makeWindowKey();
#   endif

    const int gamepad = 0;

    bool ui_mode_gamepad = false;

    while (!WindowShouldClose()) {

        if (fabsf(GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_X)) > 0 || 
            fabsf(GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_Y)) > 0 ||
            fabsf(GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_X)) > 0 ||
            fabsf(GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_Y)) > 0) {
            ui_mode_gamepad = true;
        } 

        if (Vector2Length(GetMouseDelta()) > 0) {
            ui_mode_gamepad = false;
        }

        #if DEMO_VIDEO_FEATURES
        if (IsKeyPressed(KEY_F)) {
            false_color_mode = !false_color_mode;
            needs_rebuild = true;
        }
        #endif

    #ifdef PLATFORM_WEB
        int w,h,dpi;

        EM_ASM({
            var pixelsPerPoint = 1; //window.devicePixelRatio;
            var canvas = document.getElementById('canvas');
            var width = Math.floor(pixelsPerPoint*canvas.clientWidth);
            var height = Math.floor(pixelsPerPoint*canvas.clientHeight);
            canvas.width = width;
            canvas.height = height;
            setValue($0, width, "i32");
            setValue($1, height, "i32");
            setValue($2, pixelsPerPoint, "i32");
        }, &w, &h, &dpi);

        if (GetScreenWidth() != w || GetScreenHeight() != h) {
            SetWindowSize(w, h);
        }
    #endif

        if (GetTime() - lastSave > 60) {
            save("snapshot");
        }

        if (IsFileDropped()) {
            FilePathList droppedFiles = LoadDroppedFiles();
            if (droppedFiles.count > 0) {

                openSnapshot(droppedFiles.paths[0]);
            }
            UnloadDroppedFiles(droppedFiles);
        }


        Ray ray = GetMouseRay(GetMousePosition(), camera);

        static Sphere before_edit;
        if (focusedControl == CONTROL_NONE) {
            if (mouseAction == CONTROL_NONE && selected_sphere >= 0 && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)|| IsKeyPressed(KEY_X))) {
                delete_sphere(selected_sphere);
            }

            if (selected_sphere >= 0 && IsKeyPressed(KEY_D) && (IsKeyDown(KEY_RIGHT_SUPER) || IsKeyDown(KEY_LEFT_SUPER)) && num_spheres < MAX_SPHERES) {
                spheres[num_spheres] = spheres[selected_sphere];
                selected_sphere = num_spheres;
                num_spheres++;
                needs_rebuild = true;
            }

            if (IsKeyPressed(KEY_A)) {
                add_shape();
            }

            
            if (selected_sphere >= 0 && IsKeyPressed(KEY_G)) {
                mouseAction = CONTROL_TRANSLATE;
                last_axis_set = 0;
                controlled_axis.mask = 0x7;
                before_edit = spheres[selected_sphere];
            }
            if (selected_sphere >= 0 && IsKeyPressed(KEY_R)) {
                mouseAction = CONTROL_ROTATE;
                last_axis_set = 0;
                controlled_axis.mask = 0x7;
                before_edit = spheres[selected_sphere];
            }
            if (selected_sphere >= 0 && IsKeyPressed(KEY_S)) {
                mouseAction = CONTROL_SCALE;
                last_axis_set = 0;
                controlled_axis.mask = 0x7;
                before_edit = spheres[selected_sphere];
            }
        }

        if (selected_sphere >= 0 && (mouseAction == CONTROL_TRANSLATE || mouseAction == CONTROL_ROTATE || mouseAction == CONTROL_SCALE)) {
            bool should_set = GetTime() - last_axis_set > 1;
            if (IsKeyPressed(KEY_X)) {
                controlled_axis.mask =  (should_set || !(controlled_axis.mask ^ 1)) ? 1 : (controlled_axis.mask ^ 1);
                last_axis_set = GetTime();
                spheres[selected_sphere] = before_edit;
            }
            if (IsKeyPressed(KEY_Y)) {
                controlled_axis.mask = (should_set || !(controlled_axis.mask ^ 2)) ? 2 : (controlled_axis.mask ^ 2);
                last_axis_set = GetTime();
                spheres[selected_sphere] = before_edit;
            }
            if (IsKeyPressed(KEY_Z)) {
                controlled_axis.mask = (should_set || !(controlled_axis.mask ^ 4)) ? 4 : (controlled_axis.mask ^ 4);
                last_axis_set = GetTime();
                spheres[selected_sphere] = before_edit;
            }

            if (mouseAction == CONTROL_TRANSLATE) {
            
                if (controlled_axis.x + controlled_axis.y + controlled_axis.z == 1) {
                    Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos, 
                                                               Vector3Add(spheres[selected_sphere].pos, 
                                                                          (Vector3){controlled_axis.x ,
                                                                          controlled_axis.y ,
                                                                      controlled_axis.z }),
                                                               ray.position, 
                                                               Vector3Add(ray.position, ray.direction));

                    spheres[selected_sphere].pos = nearest;
                } else {
                    Vector3 plane_normal;
                    Vector3 intersection;
                    if(controlled_axis.x + controlled_axis.y + controlled_axis.z == 2) {
                        plane_normal = (Vector3){!controlled_axis.x, !controlled_axis.y, !controlled_axis.z};
                    } else {
                        plane_normal = Vector3Subtract(camera.position, camera.target);
                    }

                    if(RayPlaneIntersection(ray.position, ray.direction, spheres[selected_sphere].pos, plane_normal, &intersection)) {
                        spheres[selected_sphere].pos = intersection;
                    }
                }

            } else if (mouseAction == CONTROL_ROTATE) {
                if (controlled_axis.x) spheres[selected_sphere].angle.x += GetMouseDelta().x/10.f;
                if (controlled_axis.y) spheres[selected_sphere].angle.y += GetMouseDelta().x/10.f;
                if (controlled_axis.z) spheres[selected_sphere].angle.z += GetMouseDelta().x/10.f;
            } else if (mouseAction == CONTROL_SCALE) {
                if (controlled_axis.x) spheres[selected_sphere].size.x *= powf(2, GetMouseDelta().x/10.f);
                if (controlled_axis.y) spheres[selected_sphere].size.y *= powf(2, GetMouseDelta().x/10.f);
                if (controlled_axis.z) spheres[selected_sphere].size.z *= powf(2, GetMouseDelta().x/10.f);
            }

            if (IsKeyPressed(KEY_ESCAPE)) {
                mouseAction = CONTROL_NONE;
                spheres[selected_sphere] = before_edit;
            }
            if (IsKeyPressed(KEY_ENTER)) mouseAction = CONTROL_NONE;
        }

        static float drag_offset;
        
        if (GetMousePosition().x > sidebar_width) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseAction == CONTROL_NONE && selected_sphere >= 0) {
                if (GetRayCollisionSphere(ray, Vector3Add(spheres[selected_sphere].pos, (Vector3){0.6,0,0}), .1).hit) {
                    Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos,
                                                         Vector3Add(spheres[selected_sphere].pos, (Vector3){1,0,0}),
                                                         ray.position,
                                                         Vector3Add(ray.position, ray.direction));

                    drag_offset = spheres[selected_sphere].pos.x - nearest.x;
                    mouseAction = CONTROL_POS_X;
                } else if (GetRayCollisionSphere(ray, Vector3Add(spheres[selected_sphere].pos, (Vector3){0,0.6,0}), .1).hit) {
                    Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos,
                                                         Vector3Add(spheres[selected_sphere].pos, (Vector3){0,1,0}),
                                                         ray.position,
                                                         Vector3Add(ray.position, ray.direction));

                    drag_offset = spheres[selected_sphere].pos.y - nearest.y;
                    mouseAction = CONTROL_POS_Y;
                } else if (GetRayCollisionSphere(ray, Vector3Add(spheres[selected_sphere].pos, (Vector3){0,0,0.6}), .1).hit) {
                    Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos,
                                                         Vector3Add(spheres[selected_sphere].pos, (Vector3){0,0,1}),
                                                         ray.position,
                                                         Vector3Add(ray.position, ray.direction));

                    drag_offset = spheres[selected_sphere].pos.z - nearest.z;
                    mouseAction = CONTROL_POS_Z;
                }
                else if (GetRayCollisionBox(ray, boundingBoxSized(Vector3Add(spheres[selected_sphere].pos,
                                                                             (Vector3){spheres[selected_sphere].size.x,0,0}), 0.2)).hit) {
                    Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos,
                                                         Vector3Add(spheres[selected_sphere].pos, (Vector3){1,0,0}),
                                                         ray.position,
                                                         Vector3Add(ray.position, ray.direction));

                drag_offset = nearest.x - spheres[selected_sphere].size.x;
                mouseAction = CONTROL_SCALE_X;
            } else if (GetRayCollisionBox(ray, boundingBoxSized(Vector3Add(spheres[selected_sphere].pos,
                                                                           (Vector3){0,spheres[selected_sphere].size.y,0}), 0.2)).hit) {
                Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos,
                                                     Vector3Add(spheres[selected_sphere].pos, (Vector3){0,1,0}),
                                                     ray.position,
                                                     Vector3Add(ray.position, ray.direction));

                drag_offset = nearest.y - spheres[selected_sphere].size.y;
                mouseAction = CONTROL_SCALE_Y;
            } else if (GetRayCollisionBox(ray, boundingBoxSized(Vector3Add(spheres[selected_sphere].pos,
                                                                           (Vector3){0,0,spheres[selected_sphere].size.z}), 0.2)).hit) {
                Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos,
                                                     Vector3Add(spheres[selected_sphere].pos, (Vector3){0,0,1}),
                                                     ray.position,
                                                     Vector3Add(ray.position, ray.direction));

                drag_offset = nearest.z - spheres[selected_sphere].size.z;
                mouseAction = CONTROL_SCALE_Z;
            }
            }

            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && mouseAction == CONTROL_NONE) {
                int new_selection = object_at_pixel((int)GetMousePosition().x, (int)GetMousePosition().y);
                if (new_selection != selected_sphere) {
                    selected_sphere = new_selection;
                    needs_rebuild = true;
                }
            }

            static Vector2 mouseDownPosition;
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) mouseDownPosition = GetMousePosition();

            if (fabsf(GetMouseWheelMove()) > 0.01 && mouseAction == CONTROL_NONE ) {
                Vector2 delta = GetMouseWheelMoveV();

                if (IsKeyDown(KEY_LEFT_ALT)) {
                    CameraMoveForward(&camera, delta.y, false);
                } else {
                    Vector3 shift = Vector3Scale(camera.up, delta.y/10);
                    camera.position = Vector3Add(camera.position, shift);
                    camera.target = Vector3Add(camera.target,shift );
                #ifdef PLATFORM_WEB
                    delta.x = -delta.x;
                #endif
                    UpdateCameraPro(&camera, (Vector3){0, -delta.x/10, 0}, Vector3Zero(), 0);
                }
            } else if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && !guiSliderDragging && mouseAction == CONTROL_NONE && Vector2Distance(mouseDownPosition, GetMousePosition()) > 1) {
                mouseAction = CONTROL_ROTATE_CAMERA;
            }

            if (mouseAction == CONTROL_ROTATE_CAMERA) {
                if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) mouseAction = CONTROL_NONE;

                Vector2 delta = GetMouseDelta();
                if (IsKeyDown(KEY_LEFT_ALT)) {
                    UpdateCameraPro(&camera, (Vector3){0, -delta.x/80, delta.y/80}, Vector3Zero(), 0);
                } else {
                    extern void CameraYaw(Camera *camera, float angle, bool rotateAroundTarget);
                    extern void CameraPitch(Camera *camera, float angle, bool lockView, bool rotateAroundTarget, bool rotateUp);
                    CameraYaw(&camera, -delta.x*0.003f, true);
                    CameraPitch(&camera, -delta.y*0.003f, true, true, false);
                }
            }

            #ifndef PLATFORM_WEB
            extern float magnification;
            if (mouseAction == CONTROL_NONE ) {
                CameraMoveForward(&camera, 8*magnification, false);
            }
            magnification = 0;
            #endif
        }

        
        const float movement_scale = 0.1;
        const float rotation_scale = 0.04;
        const float rotation_radius = 4;
        

        if (!IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_RIGHT_THUMB)) {
            Vector3 offset = Vector3Scale(GetCameraForward(&camera), -rotation_radius);
            offset = Vector3RotateByAxisAngle(offset, (Vector3){0,1,0}, -rotation_scale*GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_X));
            offset = Vector3RotateByAxisAngle(offset, GetCameraRight(&camera), -rotation_scale*GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_Y));
            camera.position = Vector3Add(offset, camera.target);
        }

        if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) {
            Vector3 up = Vector3Normalize(Vector3CrossProduct(GetCameraForward(&camera), GetCameraRight(&camera)));
            up = Vector3Scale(up, movement_scale*GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_Y));
            camera.position = Vector3Add(camera.position, up);
            camera.target = Vector3Add(camera.target, up);
        } else {
            CameraMoveForward(&camera, -GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_Y)*movement_scale, false);
        }

        CameraMoveRight(&camera, GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_X)*movement_scale, false);

        Matrix camera_matrix = GetCameraMatrix(camera);
        static Vector3 camera_space_offset;
        if (IsGamepadButtonPressed(gamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
            selected_sphere = object_at_pixel(sidebar_width + (GetScreenWidth()-sidebar_width)/2, GetScreenHeight()/2);
            needs_rebuild = true;
            if (selected_sphere>= 0){
                camera_space_offset = WorldToCamera(spheres[selected_sphere].pos, camera_matrix);
            }
        }

        if (selected_sphere >= 0) {
            if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
                spheres[selected_sphere].pos = CameraToWorld(camera_space_offset, camera_matrix);
                // spheres[selected_sphere].pos = Vector3Add(camera.position, Vector3Scale(GetCameraForward(&camera),distance));
            }

            if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
                spheres[selected_sphere].size = Vector3Scale(spheres[selected_sphere].size, 1.05);
                spheres[selected_sphere].corner_radius *= 1.05;
            }
            if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) {
                spheres[selected_sphere].size = Vector3Scale(spheres[selected_sphere].size, 0.95);
                spheres[selected_sphere].corner_radius *= 0.95;
            }


            if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) {
                if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) {
                    spheres[selected_sphere].blob_amount *= 0.95;
                }
                if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
                    spheres[selected_sphere].blob_amount = (0.01 + spheres[selected_sphere].blob_amount*1.05);
                }
            } else {
                if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) {
                    spheres[selected_sphere].corner_radius *= 0.95;
                }
                if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
                    Vector3 size = spheres[selected_sphere].size;
                    spheres[selected_sphere].corner_radius = fminf(0.01 + spheres[selected_sphere].corner_radius*1.05, fminf(size.x, fminf(size.y,size.z)));
                }
            }

            if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_RIGHT_THUMB)) {
                spheres[selected_sphere].angle = Vector3Add(spheres[selected_sphere].angle, (Vector3){
                    rotation_scale*GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_Y),
                    rotation_scale*GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_X),
                    0,
                });
            }
        }
        
        if (IsGamepadButtonDown(gamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
            if (IsGamepadButtonPressed(gamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
                add_shape();
            }

            if (selected_sphere>=0) spheres[selected_sphere].pos = Vector3Add(camera.position, Vector3Scale(GetCameraForward(&camera),8));
        }

        if (mouseAction == CONTROL_POS_X) {
            Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos, 
                                                       Vector3Add(spheres[selected_sphere].pos, (Vector3){1,0,0}),
                                                       ray.position, 
                                                       Vector3Add(ray.position, ray.direction));

            spheres[selected_sphere].pos.x = nearest.x + drag_offset;
        } else if (mouseAction == CONTROL_POS_Y) {
            Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos, 
                                                       Vector3Add(spheres[selected_sphere].pos, (Vector3){0,1,0}),
                                                       ray.position, 
                                                       Vector3Add(ray.position, ray.direction));
            spheres[selected_sphere].pos.y = nearest.y + drag_offset;
        } else if (mouseAction == CONTROL_POS_Z) {
            Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos, 
                                                       Vector3Add(spheres[selected_sphere].pos, (Vector3){0,0,1}),
                                                       ray.position, 
                                                       Vector3Add(ray.position, ray.direction));
            spheres[selected_sphere].pos.z = nearest.z + drag_offset;
        } else if (mouseAction == CONTROL_SCALE_X) {
            Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos, 
                                                       Vector3Add(spheres[selected_sphere].pos, (Vector3){1,0,0}),
                                                       ray.position, 
                                                       Vector3Add(ray.position, ray.direction));

            spheres[selected_sphere].size.x = fmaxf(0,nearest.x-drag_offset);
        } else if (mouseAction == CONTROL_SCALE_Y) {
            Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos, 
                                                       Vector3Add(spheres[selected_sphere].pos, (Vector3){0,1,0}),
                                                       ray.position, 
                                                       Vector3Add(ray.position, ray.direction));

            spheres[selected_sphere].size.y = fmaxf(0,nearest.y-drag_offset);
        } else if (mouseAction == CONTROL_SCALE_Z) {
            Vector3 nearest = NearestPointOnLine(spheres[selected_sphere].pos, 
                                                       Vector3Add(spheres[selected_sphere].pos, (Vector3){0,0,1}),
                                                       ray.position, 
                                                       Vector3Add(ray.position, ray.direction));

            spheres[selected_sphere].size.z = fmaxf(0,nearest.z-drag_offset);
        } 
          
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            mouseAction = CONTROL_NONE;
        }

        //
        // Mark: Drawing!
        //

        float deltaTime = GetFrameTime();
        runTime += deltaTime;

        if ( needs_rebuild ) {
            rebuild_shaders();
        }
        SetShaderValue(main_shader, main_locations.viewEye, &camera.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(main_shader, main_locations.viewCenter, &camera.target, SHADER_UNIFORM_VEC3);
        SetShaderValue(main_shader, main_locations.resolution, (float[2]){ (float)GetScreenWidth()*GetWindowScaleDPI().x, (float)GetScreenHeight()*GetWindowScaleDPI().y }, SHADER_UNIFORM_VEC2);
        SetShaderValue(main_shader, main_locations.runTime, &runTime, SHADER_UNIFORM_FLOAT);
        float mode = visuals_mode;
        SetShaderValue(main_shader, main_locations.visualizer, &mode, SHADER_UNIFORM_FLOAT);
        if (selected_sphere >= 0) {
            Sphere *s = &spheres[selected_sphere];
            float used_radius = fmaxf(0.01,fminf(s->corner_radius, fminf(s->size.x,fminf(s->size.y, s->size.z))));
            float data[15] = {
                s->pos.x,
                s->pos.y,
                s->pos.z,

                s->angle.x,
                s->angle.y,
                s->angle.z,

                s->size.x - used_radius,
                s->size.y - used_radius,
                s->size.z - used_radius,

                s->color.r / 255.f,
                s->color.g / 255.f,
                s->color.b / 255.f,

                used_radius,
                fmaxf(s->blob_amount, 0.0001),
                0,
            };

            SetShaderValueV(main_shader, main_locations.selectedParams, data, SHADER_UNIFORM_VEC3, 5);
            
        }

        BeginDrawing(); {
            
            ClearBackground(RAYWHITE);
            BeginShaderMode(main_shader); {
                DrawRectangle(sidebar_width, 0, GetScreenWidth()-sidebar_width, GetScreenHeight(), WHITE);
            } EndShaderMode();

            BeginMode3D(camera); {
                if (selected_sphere >= 0 && selected_sphere < MAX_SPHERES) {
                    Sphere s = spheres[selected_sphere];

                    if (mouseAction == CONTROL_TRANSLATE || mouseAction == CONTROL_ROTATE || mouseAction == CONTROL_SCALE) {
                        if (controlled_axis.x) DrawRay((Ray){Vector3Add(s.pos, (Vector3){.x=-1000}), (Vector3){.x=1}} , RED);
                        if (controlled_axis.y) DrawRay((Ray){Vector3Add(s.pos, (Vector3){.y=-1000}), (Vector3){.y=1}} , GREEN);
                        if (controlled_axis.z) DrawRay((Ray){Vector3Add(s.pos, (Vector3){.z=-1000}), (Vector3){.z=1}} , BLUE);
                    } else {

                        if (mouseAction == CONTROL_NONE || mouseAction == CONTROL_POS_X)
                            DrawLine3D(s.pos, Vector3Add(s.pos, (Vector3){0.5,0,0}),  RED);
                        if (mouseAction == CONTROL_NONE || mouseAction == CONTROL_SCALE_X)  
                            DrawCube(Vector3Add(s.pos, (Vector3){s.size.x,0,0}), .1,.1,.1, RED);
                        if ((mouseAction == CONTROL_NONE || mouseAction == CONTROL_POS_X) && !ui_mode_gamepad) 
                            DrawCylinderEx(Vector3Add(s.pos, (Vector3){0.5,0,0}),
                                           Vector3Add(s.pos, (Vector3){.7,0,0}), .1, 0, 12, RED);

                        if (mouseAction == CONTROL_NONE || mouseAction == CONTROL_POS_Y)
                            DrawLine3D(s.pos, Vector3Add(s.pos, (Vector3){0,0.5,0}),  GREEN);
                        if (mouseAction == CONTROL_NONE || mouseAction == CONTROL_SCALE_Y)  
                            DrawCube(Vector3Add(s.pos, (Vector3){0,s.size.y,0}), .1,.1,.1, GREEN);
                        if ((mouseAction == CONTROL_NONE || mouseAction == CONTROL_POS_Y) && !ui_mode_gamepad) 
                            DrawCylinderEx(Vector3Add(s.pos, (Vector3){0,0.5,0}),
                                           Vector3Add(s.pos, (Vector3){0,.7,0}), .1, 0, 12, GREEN);

                        if (mouseAction == CONTROL_NONE || mouseAction == CONTROL_POS_Z)
                            DrawLine3D(s.pos, Vector3Add(s.pos, (Vector3){0,0,0.5}),  BLUE);
                        if (mouseAction == CONTROL_NONE || mouseAction == CONTROL_SCALE_Z)  
                            DrawCube(Vector3Add(s.pos, (Vector3){0,0,s.size.z}), .1,.1,.1, BLUE);
                        if ((mouseAction == CONTROL_NONE || mouseAction == CONTROL_POS_Z) && !ui_mode_gamepad) 
                            DrawCylinderEx(Vector3Add(s.pos, (Vector3){0,0,0.5}),
                                           Vector3Add(s.pos, (Vector3){0,0,0.7}), .1, 0, 12, BLUE);

                    }
                }
            } EndMode3D();

            if (ui_mode_gamepad) {
                DrawCircle(sidebar_width + (GetScreenWidth()-sidebar_width)/2, GetScreenHeight()/2, 5, WHITE);
            }
            
            int default_color = GuiGetStyle(LABEL, TEXT);
            DrawRectangle(0, 0, sidebar_width, GetScreenHeight(), (Color){61, 61, 61,255});

            int y = 20;

            #ifndef PLATFORM_WEB
            if (GuiButton((Rectangle){20,y,80,20}, "Save")) save("save");
            if (GuiButton((Rectangle){105,y,80,20}, "Export")) export();
            y+=30;
            #endif

            GuiCheckBox((Rectangle){ 20, y+0.5, 20, 20 }, "Show Field", (bool *)&visuals_mode);
            y+=30;

            if (selected_sphere >= 0 ){
                Sphere old = spheres[selected_sphere];


                GuiLabel((Rectangle){ 20, y, 92, 24 }, "Position");
                y+=18;
                GuiSetStyle(LABEL, TEXT, 0xff0000ff);
                if(GuiFloatValueBox((Rectangle){ 20, y, 50, 20 }, "X", &spheres[selected_sphere].pos.x, -50, 50, focusedControl == CONTROL_POS_X)) focusedControl = (focusedControl == CONTROL_POS_X) ? CONTROL_NONE : CONTROL_POS_X;
                GuiSetStyle(LABEL, TEXT, 0x00ff00ff);
                if(GuiFloatValueBox((Rectangle){ 85, y, 50, 20 }, "Y", &spheres[selected_sphere].pos.y, -50, 50, focusedControl == CONTROL_POS_Y)) focusedControl = (focusedControl == CONTROL_POS_Y) ? CONTROL_NONE : CONTROL_POS_Y;
                GuiSetStyle(LABEL, TEXT, 0x0000ffff);
                if(GuiFloatValueBox((Rectangle){ 150, y, 50, 20 }, "Z", &spheres[selected_sphere].pos.z, -50, 50, focusedControl == CONTROL_POS_Z)) focusedControl = (focusedControl == CONTROL_POS_Z) ? CONTROL_NONE : CONTROL_POS_Z;
                GuiSetStyle(LABEL, TEXT, default_color);
                
                y+=23;
                GuiLabel((Rectangle){ 20, y, 92, 24 }, "Scale");
                y+=18;
                GuiSetStyle(LABEL, TEXT, 0xff0000ff);
                if(GuiFloatValueBox((Rectangle){ 20, y, 50, 20 }, "X", &spheres[selected_sphere].size.x, 0, 50, focusedControl == CONTROL_SCALE_X)) focusedControl = (focusedControl == CONTROL_SCALE_X) ? CONTROL_NONE : CONTROL_SCALE_X;
                GuiSetStyle(LABEL, TEXT, 0x00ff00ff);
                if(GuiFloatValueBox((Rectangle){ 85, y, 50, 20 }, "Y", &spheres[selected_sphere].size.y, 0, 50, focusedControl == CONTROL_SCALE_Y)) focusedControl = (focusedControl == CONTROL_SCALE_Y) ? CONTROL_NONE : CONTROL_SCALE_Y;
                GuiSetStyle(LABEL, TEXT, 0x0000ffff);
                if(GuiFloatValueBox((Rectangle){ 150, y, 50, 20 }, "Z", &spheres[selected_sphere].size.z, 0, 50, focusedControl == CONTROL_SCALE_Z)) focusedControl = (focusedControl == CONTROL_SCALE_Z) ? CONTROL_NONE : CONTROL_SCALE_Z;
                GuiSetStyle(LABEL, TEXT, default_color);

                y+=23;
                GuiLabel((Rectangle){ 20, y, 92, 24 }, "Rotation");
                y+=18;
                GuiSetStyle(LABEL, TEXT, 0xff0000ff);
                if(GuiFloatValueBox((Rectangle){ 20, y, 50, 20 }, "X", &spheres[selected_sphere].angle.x, -360, 360, focusedControl == CONTROL_ANGLE_X)) focusedControl = (focusedControl == CONTROL_ANGLE_X) ? CONTROL_NONE : CONTROL_ANGLE_X;
                GuiSetStyle(LABEL, TEXT, 0x00ff00ff);
                if(GuiFloatValueBox((Rectangle){ 85, y, 50, 20 }, "Y", &spheres[selected_sphere].angle.y, -360, 360, focusedControl == CONTROL_ANGLE_Y)) focusedControl = (focusedControl == CONTROL_ANGLE_Y) ? CONTROL_NONE : CONTROL_ANGLE_Y;
                GuiSetStyle(LABEL, TEXT, 0x0000ffff);
                if(GuiFloatValueBox((Rectangle){ 150, y, 50, 20 }, "Z", &spheres[selected_sphere].angle.z, -360, 360, focusedControl == CONTROL_ANGLE_Z)) focusedControl = (focusedControl == CONTROL_ANGLE_Z) ? CONTROL_NONE : CONTROL_ANGLE_Z;
                GuiSetStyle(LABEL, TEXT, default_color);

                Vector3 hsv = ColorToHSV((Color){spheres[selected_sphere].color.r, spheres[selected_sphere].color.g, spheres[selected_sphere].color.b, 255});
                Vector3 original_hsv = hsv;

                y+=23;
                GuiLabel((Rectangle){ 20, y, 92, 24 }, "Color");
                y+=20;
                GuiSetStyle(LABEL, TEXT, 0xff0000ff);
                if(GuiSlider((Rectangle){ 30, y, 155, 16 }, "H", "", &hsv.x, 0, 360)) focusedControl = (focusedControl == CONTROL_COLOR_R) ? CONTROL_NONE : CONTROL_COLOR_R;
                GuiSetStyle(LABEL, TEXT, 0x00ff00ff);

                y+=19;
                if(GuiSlider((Rectangle){ 30, y, 155, 16 }, "S", "", &hsv.y, 0.01, 1)) focusedControl = (focusedControl == CONTROL_COLOR_G) ? CONTROL_NONE : CONTROL_COLOR_G;
                GuiSetStyle(LABEL, TEXT, 0x0000ffff);

                y+=19;
                if(GuiSlider((Rectangle){ 30, y, 155, 16 }, "B", "", &hsv.z, 0.01, 1)) focusedControl = (focusedControl == CONTROL_COLOR_B) ? CONTROL_NONE : CONTROL_COLOR_B;
                GuiSetStyle(LABEL, TEXT, default_color);

                if (memcmp(&original_hsv, &hsv, sizeof(hsv))) {
                    Color new = ColorFromHSV(hsv.x,hsv.y,hsv.z);
                    spheres[selected_sphere].color.r = new.r;
                    spheres[selected_sphere].color.g = new.g;
                    spheres[selected_sphere].color.b = new.b;
                }

                y+=23;
                if (GuiFloatValueBox((Rectangle){ 40, y, 40, 20 }, "blob", &spheres[selected_sphere].blob_amount, 0, 10, focusedControl == CONTROL_BLOB_AMOUNT)) focusedControl = (focusedControl == CONTROL_BLOB_AMOUNT) ? CONTROL_NONE : CONTROL_BLOB_AMOUNT;
                if (GuiFloatValueBox((Rectangle){ 140, y, 70, 20 }, "Roundness", &spheres[selected_sphere].corner_radius, 0, 9999, focusedControl == CONTROL_CORNER_RADIUS)) focusedControl = (focusedControl == CONTROL_CORNER_RADIUS) ? CONTROL_NONE : CONTROL_CORNER_RADIUS;

                GuiCheckBox((Rectangle){ 120, y+=23, 20, 20 }, "cut out", &spheres[selected_sphere].subtract);

                GuiLabel((Rectangle){ 20, y+=23, 92, 24 }, "Mirror");
                GuiSetStyle(LABEL, TEXT, 0xff0000ff);
                GuiCheckBox((Rectangle){ 20, y+=23, 20, 20 }, "x", &spheres[selected_sphere].mirror.x);
                GuiSetStyle(LABEL, TEXT, 0x00ff00ff);
                GuiCheckBox((Rectangle){ 70, y, 20, 20 }, "y", &spheres[selected_sphere].mirror.y);
                GuiSetStyle(LABEL, TEXT, 0x0000ffff);
                GuiCheckBox((Rectangle){ 120, y, 20, 20 }, "z", &spheres[selected_sphere].mirror.z);
                GuiSetStyle(LABEL, TEXT, default_color);

                if (memcmp(&old.mirror, &spheres[selected_sphere].mirror, sizeof(old.mirror)) ||
                 old.subtract != spheres[selected_sphere].subtract) {
                    needs_rebuild = true;

                    BoundingBox bb = shapeBoundingBox(spheres[selected_sphere]);
                    if (spheres[selected_sphere].mirror.x && bb.max.x <= 0) {
                        spheres[selected_sphere].pos.x *= -1;
                        spheres[selected_sphere].angle.y *= -1;
                        spheres[selected_sphere].angle.z *= -1;
                    }

                    if (spheres[selected_sphere].mirror.y && bb.max.y <= 0) {
                        spheres[selected_sphere].pos.y *= -1;
                        spheres[selected_sphere].angle.x *= -1;
                        spheres[selected_sphere].angle.z *= -1;
                    }

                    if (spheres[selected_sphere].mirror.z && bb.max.z <= 0) {
                        spheres[selected_sphere].pos.z *= -1;
                        spheres[selected_sphere].angle.y *= -1;
                        spheres[selected_sphere].angle.x *= -1;
                    }
                }

                if (memcmp(&old.color, &spheres[selected_sphere].color, sizeof(old.color))) {
                    last_color_set = (Color){
                        spheres[selected_sphere].color.r,
                        spheres[selected_sphere].color.g,
                        spheres[selected_sphere].color.b,
                        0
                    };
                }

                // if (memcmp(&old, &spheres[selected_sphere], sizeof(Sphere))) needs_rebuild = true;

                GuiSetState(STATE_NORMAL);
            }
            y+=30;


            const int row_height = 30;
            Rectangle scrollArea = (Rectangle){0, y, sidebar_width, GetScreenHeight() - y};

            int tempTextAlignment = GuiGetStyle(BUTTON, TEXT_ALIGNMENT);
            int border_width = GuiGetStyle(BUTTON, BORDER_WIDTH);
            int text_padding = GuiGetStyle(BUTTON, BORDER_WIDTH);
            GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
            GuiSetStyle(DEFAULT, BORDER_WIDTH, 0);
            GuiSetStyle(BUTTON, TEXT_PADDING, 8);
            default_color = GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL);

            GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0);
            Rectangle view_area;
            GuiScrollPanel(scrollArea, NULL, (Rectangle){0,0,scrollArea.width-15, num_spheres*row_height}, &scroll_offset, &view_area); // Scroll Panel control
            BeginScissorMode((int)view_area.x, (int)view_area.y, (int)view_area.width, (int)view_area.height); {
                const int first_visible = (int)floorf(-scroll_offset.y / row_height);
                const int last_visible = first_visible + (int)ceilf(view_area.height / row_height);
                for (int i=first_visible; i < MIN(last_visible+1,num_spheres); i++) {
                    Sphere *s  = &spheres[i];
                    const char *text = TextFormat("%c Shape %i", s->subtract ? '-' : '+', i+1);

                    if (selected_sphere == i) GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x444444ff);

                    if (GuiButton((Rectangle){10, scrollArea.y+i*row_height+scroll_offset.y, sidebar_width+10,row_height }, text)) {
                        selected_sphere = i;
                        needs_rebuild = true;
                    }

                    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0);
                }
            } EndScissorMode();
            GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, default_color);
            GuiSetStyle(BUTTON, TEXT_ALIGNMENT, tempTextAlignment);
            GuiSetStyle(DEFAULT, BORDER_WIDTH, border_width);
            GuiSetStyle(BUTTON, TEXT_PADDING, text_padding);


            if (focusedControl != CONTROL_NONE) {
                DrawText("Nudge Value: Up & Down Arrows    Cancel: Escape    Done: Enter", sidebar_width + 8, 11, 10, WHITE);
            } else if (mouseAction == CONTROL_NONE) {
                DrawText("Add Shape: A    Delete: X    Grab: G    Rotate: R    Scale: S    Camera: Click+Drag", sidebar_width + 8, 11, 10, WHITE);
            } else if (mouseAction == CONTROL_TRANSLATE || mouseAction == CONTROL_ROTATE || mouseAction == CONTROL_SCALE) {
                DrawText("Change axis: X Y Z    Cancel: Escape    Done: Enter", sidebar_width + 8, 11, 10, WHITE);
            } else if (mouseAction == CONTROL_ROTATE_CAMERA) {
                DrawText("Pan: Alt+Drag", sidebar_width + 8, 11, 10, WHITE);
            } 

        } EndDrawing();
    }

    return 0;
}
