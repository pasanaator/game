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
#define FILENAME "assets\\test.gmap"

int W_WIDTH = 800;
int W_HEIGHT = 600;
bool STRETCH = FALSE;

int clientWidth = 0;
int clientHeight = 0;
const int TILE_SIZE = 50;

uint8_t mapWidth = 0;
uint8_t mapHeight = 0;
size_t totalTiles = 0;
uint8_t total_levels = 0;

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
    bool pad1 : 1;
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

static Object *wall = NULL;
int WallCount = 0;

uint16_t LevelIndex = 0;

MapTile* GetTilePtr(MapTile *map, uint16_t row, uint16_t col) {
    if (row >= mapHeight || col >= mapWidth) {
        return NULL;
    }
    return &map[(row * mapWidth) + col];
}

bool SetTile(MapTile *map, uint16_t row, uint16_t col, uint8_t tile) {
    if (row >= mapHeight || col >= mapWidth) {
        return false;
    }
    map[(row * mapWidth) + col].tile = tile;
    return true;
}

int InitObjects(Object **o,MapTile *map, uint8_t id)  {
    totalTiles = (size_t)mapWidth * mapHeight;
    if (totalTiles == 0 || !map) {
        *o = NULL;
        return 0;
    }

    *o = malloc(totalTiles * sizeof(Object));
    if (*o == NULL) {
        perror("Failed to allocate *o\n");
        exit(1);
    }

    int ObjectCount = 0;
    for (size_t i = 0; i < totalTiles; i++) {
        if (map[i].tile == id) {
            (*o)[ObjectCount].x = (uint16_t)(i % mapWidth);
            (*o)[ObjectCount].y = (uint16_t)(i / mapWidth);
            ObjectCount++;
        }
    }
    if (ObjectCount == 0) {
        free(*o);
        *o = NULL;
        return 0;
    }
    Object *temp = realloc(*o, ObjectCount * sizeof(Object));
    if (temp) {
        *o = temp;
    }
    return ObjectCount;
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
    if (wall) {
        free(wall);
        wall = NULL;
        WallCount = 0;
    }

    if (ActiveMap) {
        free(ActiveMap);
        ActiveMap = NULL;
    }
}

bool InitMap(uint8_t LevelIndex) {
    if (LevelIndex > total_levels) {
        perror("Invalid level index");
        return FALSE;
    }

    UnloadMap();
    if (!LoadMapFromFile(FILENAME, LevelIndex, &ActiveMap)) {
        return FALSE;
    }

    WallCount = InitObjects(&wall, ActiveMap, 1);
    return TRUE;
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

    if (GetAsyncKeyState('0') & 0x8000) InitMap(0);
    if (GetAsyncKeyState('1') & 0x8000) InitMap(1);
    if (GetAsyncKeyState('2') & 0x8000) InitMap(2);
    if (GetAsyncKeyState('3') & 0x8000) InitMap(3);
    if (GetAsyncKeyState('4') & 0x8000) InitMap(4);

    player.movement.dirX = 0.0f;
    player.movement.dirY = 0.0f;

    float forward = 0.0f;
    float strafe  = 0.0f;

    float rotSpeed = 0.05f;
    if (!Death) {
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

    // X-axis movement + collision
    if (moveDeltaX != 0.0f) {
        float offsetX = (moveDeltaX > 0.0f) ? radius : -radius;
        int checkGridX = (int)((newX + offsetX) / (float)TILE_SIZE);
        int topGridY   = (int)((player.y - radius * 0.5f) / (float)TILE_SIZE);
        int botGridY   = (int)((player.y + radius * 0.5f) / (float)TILE_SIZE);

        if (checkGridX >= 0 && checkGridX < mapWidth && topGridY >= 0 && botGridY < mapHeight) {
            int tileTop = ActiveMap[topGridY * mapWidth + checkGridX].tile;
            int tileBot = ActiveMap[botGridY * mapWidth + checkGridX].tile;
            if (ActiveMap[topGridY * mapWidth + checkGridX].isDeadly || ActiveMap[botGridY * mapWidth + checkGridX].isDeadly) {
                Death = TRUE;
            }
            if (!(tileTop >= 1 && tileBot >= 1)) {
                player.x = newX;
            }
        }
    }

    // Y-axis movement + collision
    if (moveDeltaY != 0.0f) {
        float offsetY = (moveDeltaY > 0.0f) ? radius : -radius;
        int checkGridY = (int)((newY + offsetY) / (float)TILE_SIZE);
        int leftGridX  = (int)((player.x - radius * 0.5f) / (float)TILE_SIZE);
        int rightGridX = (int)((player.x + radius * 0.5f) / (float)TILE_SIZE);

        if (leftGridX >= 0 && rightGridX < mapWidth && checkGridY >= 0 && checkGridY < mapHeight) {
            int tileLeft  = ActiveMap[checkGridY * mapWidth + leftGridX].tile;
            int tileRight = ActiveMap[checkGridY * mapWidth + rightGridX].tile;
            if (ActiveMap[checkGridY * mapWidth + leftGridX].isDeadly || ActiveMap[checkGridY * mapWidth + rightGridX].isDeadly) {
                Death = TRUE;
            }
            if (!(tileLeft >= 1 && tileRight >= 1)) {
                player.y = newY;
            }
        }
    }

    float maxWorldX = (float)(mapWidth * TILE_SIZE) - 1.0f;
    float maxWorldY = (float)(mapHeight * TILE_SIZE) - 1.0f;

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

void drawText(const char *text, const char *font, uint8_t fontSize, uint16_t x, uint16_t y, HDC hdc, HDC memDC, uint8_t tR, uint8_t tG, uint8_t tB, uint8_t bR, uint8_t bG, uint8_t bB, const int MODE) {
    SetTextColor(memDC, RGB(tR, tG, tB));
    SetBkColor(memDC, RGB(bR, bG, bB));
    SetBkMode(memDC, MODE);

    HFONT hFont = CreateFont(
        fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
    CLEARTYPE_QUALITY, VARIABLE_PITCH, font
    );
    HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);

    TextOut(memDC, x, y, text , strlen(text));
    SelectObject(memDC, hOldFont);
    DeleteObject(hFont);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            StopGameAudio();
            PostQuitMessage(0);
            return 0;
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
                        drawText("YOU'RE DEAD!", "Terminal", 56, clientWidth / 2, clientHeight / 2, hdc, memDC, 255, 0, 0, 0, 0, 0, OPAQUE);
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
    if (__argc >= 4) {
        W_WIDTH = atoi(__argv[1]);
        W_HEIGHT = atoi(__argv[2]);
        if (__argv[3]) {
            STRETCH == TRUE;
        }
    }

    PlayGameAudio("assets\\backround.mp3", AUDIO_VOLUME);
    if (!InitMap(0)) return 1;

    const char CLASS_NAME[] = "Test";

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
        "Test",
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
            UpdateGame();
            InvalidateRect(hwnd, NULL, FALSE);
            Sleep(16);
        }
    }

    return 0;
}