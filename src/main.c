#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <mmsystem.h>
#include <math.h>
#include <stdbool.h>

#include "renderer.h"

#define AUDIO_VOLUME 125

#define MAP_VERSION 1
#define FILENAME "assets\\level.gmap"

#ifndef defDEBUG
    #define defDEBUG FALSE
#endif

int W_WIDTH = 800;
int W_HEIGHT = 600;
int clientWidth = 0;
int clientHeight = 0;
bool STRETCH = FALSE;
const int TILE_SIZE = 50;
bool isFocused = false;
bool isCursorHidden = false;
bool DEBUG = defDEBUG;

uint8_t mapWidth = 0;
uint8_t mapHeight = 0;
size_t totalTiles = 0;
uint8_t total_levels = 0;
uint8_t LevelIndex = 0;

uint32_t *wallTexture = NULL;
uint32_t *BrickwallTexture = NULL;
uint32_t *VentTexture = NULL;

typedef struct {
    float speed, currentSpeed, acceleration, maxAcceleration, accelerationGrowth, decelerationRate, dirY, dirX;
} Movement;

typedef struct {
    float x, y;
    float angle;
    Movement movement;
} Player;

typedef struct {
    uint8_t tile;
    bool isDeadly : 1;
    bool isLevelTransition : 1;
    bool pad2 : 1;
    bool pad3 : 1;
    bool pad4 : 1;
    bool pad5 : 1;
    bool pad6 : 1;
    bool pad7 : 1;
} MapTile;

typedef struct {
    uint8_t version;
    uint8_t width;
    uint8_t height;
    uint8_t total_levels;
    uint32_t pad;
} MapHeader;

typedef struct {
    uint16_t x, y;
    uint8_t sym;
} Object;

MapTile *ActiveMap = NULL;

uint32_t *LoadTextureFile(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("Failed to open file!");
        return NULL;
    }

    unsigned char header[54];
    if (fread(header, 1, 54, f) != 54) {
        perror("Not a valid BMP");
        fclose(f);
        return NULL;
    }

    int width = *(int *)&header[18];
    int height = *(int *)&header[22];
    int bpp = *(short *)&header[28];
    int offset = *(int *)&header[10];

    fseek(f, offset, SEEK_SET);

    uint32_t *texture = malloc((width * height) * sizeof(uint32_t));
    if (!texture) {
        perror("Malloc failed");
        fclose(f);
        return NULL;
    }

    int row_stride = ((width * (bpp / 8) + 3) / 4) * 4;
    unsigned char *row_buffer = malloc(row_stride);

    for (int y = height - 1; y >= 0; y--) {
        fread(row_buffer, 1, row_stride, f);
        for (int x = 0; x < width; x++) {
            int index = x * (bpp / 8);
            
            unsigned char b = row_buffer[index + 0];
            unsigned char g = row_buffer[index + 1];
            unsigned char r = row_buffer[index + 2];
            unsigned char a = (bpp == 32) ? row_buffer[index + 3] : 255;

            texture[y * (width) + x] = ((uint32_t)a << 24)       | 
                                            ((uint32_t)r << 16)  | 
                                            ((uint32_t)g << 8)   | 
                                            ((uint32_t)b);
        }
    }

    free(row_buffer);
    fclose(f);
    return texture;
}

bool LoadMapFromFile(const char *filename, uint16_t level_index, MapTile **dest_buffer) {

    if (!filename || !dest_buffer) {
        return false;
    }

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open map file");
        return false;
    }

    MapHeader header;
    if (fread(&header, sizeof(MapHeader), 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    if (header.version != MAP_VERSION) {
        fprintf(stderr, "Map version mismatch!");
        fclose(fp);
        return false;
    }
    if (header.width == 0 || header.height == 0) {
        fprintf(stderr, "Invalid map dimensions: %ux%u\n", header.width, header.height);
        fclose(fp);
        return false;
    }

    if (level_index >= header.total_levels) {
        fprintf(stderr, "Level %u out of bounds (Max: %u)\n", level_index, header.total_levels - 1);
        fclose(fp);
        return false;
    }

    // Set global dimensions from file header
    mapWidth = header.width;
    mapHeight = header.height;
    total_levels = header.total_levels;
    size_t totalTiles = (size_t)mapWidth * mapHeight;

    long offset = (long)sizeof(MapHeader) + (long)(level_index * totalTiles * sizeof(MapTile));
    if (fseek(fp, offset, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    *dest_buffer = malloc(totalTiles * sizeof(MapTile));
    if (!(*dest_buffer)) {
        perror("Failed to allocate destination map buffer");
        fclose(fp);
        return false;
    }

    size_t read_count = fread(*dest_buffer, sizeof(MapTile), totalTiles, fp);
    fclose(fp);

    if (read_count != totalTiles) {
        free(*dest_buffer);
        *dest_buffer = NULL;
        return false;
    }

    return true;
}

void UnloadMap() {
    if (wallTexture) { free(wallTexture); wallTexture = NULL; }
    if (BrickwallTexture) { free(BrickwallTexture); BrickwallTexture = NULL; }
    if (VentTexture) { free(VentTexture); VentTexture = NULL; }

    if (ActiveMap) {
        free(ActiveMap);
        ActiveMap = NULL;
    }
}

bool InitMap(uint8_t levelIndex) {
    if (levelIndex > total_levels) {
        perror("Invalid level index");
        return FALSE;
    }

    UnloadMap();
    if (!LoadMapFromFile(FILENAME, levelIndex, &ActiveMap)) {
        return FALSE;
    }

    wallTexture = LoadTextureFile("assets\\wall.bmp");
    BrickwallTexture = LoadTextureFile("assets\\brick_wall.bmp");
    VentTexture = LoadTextureFile("assets\\vent.bmp");

    return TRUE;
}

typedef struct {
    bool isColliding;
    bool isDeadly;
    bool isLevelTransition;
} Collision;

Collision coll = {NULL};

Collision isColliding(float x, float y, float r) {
    int left   = (int)floorf((x - r) / TILE_SIZE);
    int right  = (int)floorf((x + r) / TILE_SIZE);
    int top    = (int)floorf((y - r) / TILE_SIZE);
    int bottom = (int)floorf((y + r) / TILE_SIZE);

    Collision fColl = {NULL};
    if (ActiveMap[top * mapWidth + left].tile > 1 || ActiveMap[top * mapWidth + right].tile > 1 || ActiveMap[bottom * mapWidth + left].tile > 1 || ActiveMap[bottom * mapWidth + right].tile > 1)
        fColl.isColliding = TRUE;
    if (ActiveMap[top * mapWidth + left].isDeadly || ActiveMap[top * mapWidth + right].isDeadly || ActiveMap[bottom * mapWidth + left].isDeadly || ActiveMap[bottom * mapWidth + right].isDeadly)
        fColl.isDeadly = TRUE;
    if (ActiveMap[top * mapWidth + left].isLevelTransition || ActiveMap[top * mapWidth + right].isLevelTransition || ActiveMap[bottom * mapWidth + left].isLevelTransition || ActiveMap[bottom * mapWidth + right].isLevelTransition)
        fColl.isLevelTransition = TRUE;
    return fColl;
}

Player player = {
    .x = 3.0f * TILE_SIZE,
    .y = 2.0f * TILE_SIZE,
    .movement = {
        .speed = 0.5f,
        .acceleration = 1.0f,
        .accelerationGrowth = 0.04f,
        .maxAcceleration = 2.0f,
        .decelerationRate = 0.05f,
        .dirX = 0.0f,
        .dirY = 0.0f
    }
};

bool Death = FALSE;

void UpdateGame() {

    player.movement.dirX = 0.0f;
    player.movement.dirY = 0.0f;

    float forward = 0.0f;
    float strafe  = 0.0f;
    float rotSpeed = 0.05f;

    if (!Death) {
        if(DEBUG) {
            if (GetAsyncKeyState('0') & 0x8000) InitMap(LevelIndex = 0);
            if (GetAsyncKeyState('1') & 0x8000) InitMap(LevelIndex = 1);
            if (GetAsyncKeyState('2') & 0x8000) InitMap(LevelIndex = 2);
            if (GetAsyncKeyState('3') & 0x8000) InitMap(LevelIndex = 3);
            if (GetAsyncKeyState('4') & 0x8000) InitMap(LevelIndex = 4);
        }

        if (GetAsyncKeyState(VK_LEFT) & 0x8000)  player.angle -= rotSpeed;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) player.angle += rotSpeed;

        if (GetAsyncKeyState('W') & 0x8000) forward += 1.0f;
        if (GetAsyncKeyState('S') & 0x8000) forward -= 1.0f;
        if (GetAsyncKeyState('D') & 0x8000) strafe  += 1.0f;
        if (GetAsyncKeyState('A') & 0x8000) strafe  -= 1.0f;
    }

    if (forward != 0.0f && strafe != 0.0f) {
    forward *= 0.7071f;
    strafe  *= 0.7071f;
    }

    player.movement.dirX = forward;
    player.movement.dirY = strafe;

    BOOL isMoving = (player.movement.dirX != 0.0f || player.movement.dirY != 0.0f);

    if (isMoving) {
        if (player.movement.acceleration < player.movement.maxAcceleration) {
            player.movement.acceleration += player.movement.accelerationGrowth;
            if (player.movement.acceleration > player.movement.maxAcceleration) {
                player.movement.acceleration = player.movement.maxAcceleration;
            }
        }
    } else {
        if (player.movement.acceleration > 1.0f) {
            player.movement.acceleration -= player.movement.decelerationRate;
            if (player.movement.acceleration < 1.0f) {
                player.movement.acceleration = 1.0f;
            }
        }
    }


    player.movement.currentSpeed = player.movement.speed * (player.movement.acceleration * player.movement.acceleration);

    float cosA = cosf(player.angle);
    float sinA = sinf(player.angle);

    float moveDeltaX = (cosA * forward - sinA * strafe) * player.movement.currentSpeed;
    float moveDeltaY = (sinA * forward + cosA * strafe) * player.movement.currentSpeed;
    
    float radius = 15.0f;

    float newX = player.x + moveDeltaX;
    float newY = player.y + moveDeltaY;

    // Collision
    if (moveDeltaX != 0.0f || moveDeltaY != 0.0f) {
        // --- X-Axis Movement ---
        float oldX = player.x;
        player.x = newX;
        if (isColliding(player.x, player.y, radius).isColliding) {
            player.x = oldX; // Safe: never lets you tunnel through X walls
        }

        // --- Y-Axis Movement ---
        float oldY = player.y;
        player.y = newY;
        coll = isColliding(player.x, player.y, radius);
        if (coll.isColliding) {
            player.y = oldY; // Safe: never lets you tunnel through Y walls
        }
        if (coll.isDeadly) Death = TRUE;
        if (coll.isLevelTransition) InitMap(LevelIndex++);
    }
    

    float maxWorldX = ((float)(mapWidth * TILE_SIZE) - 1.0f);
    float maxWorldY = ((float)(mapHeight * TILE_SIZE) - 1.0f);

    if (player.x < 1.0f) player.x = 1.0f;
    else if (player.x > maxWorldX) player.x = maxWorldX;

    if (player.y < 1.0f) player.y = 1.0f;
    else if (player.y > maxWorldY) player.y = maxWorldY;

    if (Death) {
        static uint16_t counter = 0;
        // Counter uses time in frames passed, at a nominal 60FPS this equates to 2 seconds
        if (counter == 120) {
            // Temporarily using hardcoded values
            player.x = 3.0f * TILE_SIZE;
            player.y = 2.0f * TILE_SIZE;
            InitMap(0);
            Death = FALSE;
            counter = 0;
        }
        counter++;
    }
}


void PlayGameAudio(const TCHAR* filePath, int volumeLevel) {
    TCHAR command[260];

    mciSendString(TEXT("close backgroundAudio"), NULL, 0, NULL);

    wsprintf(command, TEXT("open \"%s\" type mpegvideo alias backgroundAudio"), filePath);
    
    if (mciSendString(command, NULL, 0, NULL) != 0) {
        MessageBox(NULL, TEXT("Failed to open audio file!"), TEXT("Audio Error"), MB_OK | MB_ICONERROR);
        return;
    }

    TCHAR volCommand[64];
    wsprintf(volCommand, TEXT("setaudio backgroundAudio volume to %d"), volumeLevel);
    mciSendString(volCommand, NULL, 0, NULL);

    if (mciSendString(TEXT("play backgroundAudio repeat"), NULL, 0, NULL) != 0) {
        MessageBox(NULL, TEXT("Failed to play audio!"), TEXT("Audio Error"), MB_OK | MB_ICONERROR);
    }
}

void StopGameAudio() {
    mciSendString(TEXT("stop backgroundAudio"), NULL, 0, NULL);
    mciSendString(TEXT("close backgroundAudio"), NULL, 0, NULL);
}

void UpdateCursorState() {
    if (isFocused) {
        if (!isCursorHidden) {
            ShowCursor(FALSE);
            isCursorHidden = true;
        }
    } else {
        if (isCursorHidden) {
            ShowCursor(TRUE);
            isCursorHidden = false;
        }
    }
}

HBITMAP CreateBlankDIB(HDC hdc, int width, int height, void** ppvBits) {
    BITMAPINFO bmi;

    ZeroMemory(&bmi, sizeof(BITMAPINFO));

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HBITMAP hBitmap = CreateDIBSection(
        hdc,
        &bmi,
        DIB_RGB_COLORS,
        ppvBits,
        NULL,
        0
    );

    return hBitmap;
}

void drawText(const char *text, const char *font, uint8_t fontSize, uint16_t x, uint16_t y, bool center, HDC memDC, uint8_t tR, uint8_t tG, uint8_t tB, uint8_t bR, uint8_t bG, uint8_t bB, const int MODE) {
    SetTextColor(memDC, RGB(tR, tG, tB));
    SetBkColor(memDC, RGB(bR, bG, bB));
    SetBkMode(memDC, MODE);

    HFONT hFont = CreateFont(
        fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
    CLEARTYPE_QUALITY, VARIABLE_PITCH, font
    );
    HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);

    if (center) {
    SIZE textSize;
    GetTextExtentPoint32(memDC, text, (int)strlen(text), &textSize);
    x = (clientWidth - textSize.cx) / 2;
    y = (clientHeight - textSize.cy) / 2;
    }

    TextOut(memDC, x, y, text , strlen(text));
    SelectObject(memDC, hOldFont);
    DeleteObject(hFont);
}



LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY: {
            if (isCursorHidden) ShowCursor(TRUE);
            ClipCursor(NULL);
            StopGameAudio();
            PostQuitMessage(0);
            return 0;
        }
        case WM_ACTIVATE: {
            isFocused = (LOWORD(wParam) != WA_INACTIVE);
            UpdateCursorState();
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            if (clientWidth > 0 && clientHeight > 0) {
                int renderWidth = STRETCH ? W_WIDTH : clientWidth;
                int renderHeight = STRETCH ? W_HEIGHT : clientHeight;

                void* ppvbits = NULL;
                HBITMAP memBitmap = CreateBlankDIB(hdc, renderWidth, renderHeight, &ppvbits);

                if (memBitmap && ppvbits) {
                    HDC memDC = CreateCompatibleDC(hdc);
                    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

                    // Render game into our buffer
                    render_game((DWORD*)ppvbits, renderWidth, renderHeight);

                    if (Death) {
                        drawText("YOU'RE DEAD!", "Terminal", 72, 0, 0, TRUE, memDC, 255, 0, 0, 0, 0, 0, OPAQUE);
                    }

                    if (STRETCH) {
                        SetStretchBltMode(hdc, HALFTONE);
                        StretchBlt(
                            hdc, 0, 0, clientWidth, clientHeight,
                            memDC, 0, 0, W_WIDTH, W_HEIGHT,
                            SRCCOPY
                        );
                    } else {
                        BitBlt(
                            hdc, 0, 0, clientWidth, clientHeight,
                            memDC, 0, 0,
                            SRCCOPY
                        );
                    }

                    SelectObject(memDC, oldBitmap);
                    DeleteDC(memDC);
                    DeleteObject(memBitmap);
                }
            }
        EndPaint(hwnd, &ps);
        return 0;
        }
        case WM_SIZE:
            clientWidth = LOWORD(lParam);
            clientHeight = HIWORD(lParam);
            return 0;
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (strstr(lpCmdLine, "-s") != NULL) {
            STRETCH = TRUE;
    }

    if (strstr(lpCmdLine, "-d") != NULL) {
            DEBUG = TRUE;
    }

    if (strstr(lpCmdLine, "--help") != NULL) {
            printf("--help -> Prints help\n -s -> STRETCH=TRUE\n -d -> DEBUG=TRUE\n -w int -> W_WIDTH\n -h int -> W_HEIGHT\n");
    }

    char *lpWWidth = strstr(lpCmdLine, "-w");
    if (lpWWidth != NULL) {
        sscanf(lpWWidth, "-w %d", &W_WIDTH);
    }

    char *lpWHeight = strstr(lpCmdLine, "-h");
    if (lpWHeight != NULL) {
        sscanf(lpWHeight, "-w %d", &W_WIDTH);
    }




    PlayGameAudio("assets\\backround.mp3", AUDIO_VOLUME);
    if (!InitMap(0)) return 0;

    const char CLASS_NAME[] = "Game";

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassA(&wc)) {
        return 0;
    }

    clientWidth = W_WIDTH;
    clientHeight = W_HEIGHT;
    
    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT wr = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRect(&wr, style, FALSE);

    HWND hwnd = CreateWindowExA(
        0,
        CLASS_NAME,
        "Game",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            if (isFocused) UpdateGame();
            InvalidateRect(hwnd, NULL, FALSE);
            Sleep(16);
        }
    }

    return 0;
}