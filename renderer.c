#include "renderer.h"
#include <math.h>
#include <windows.h>

//--- Configuration Constants ---
#define WIDTH 800
#define HEIGHT 600
#define GRID_SIZE 80
#define WORLD_WIDTH 10
#define WORLD_HEIGHT 10

//--- External References to Map & Player ---
extern int *grid; // Points to your friend's 1D dynamic map array
extern int player_radius;

//--- Ray Structure Definition ---
typedef struct {
    float rayDirX, rayDirY;         // Ray direction vector components
    float deltaDistX, deltaDistY;   // Distance ray travels to cross one grid X/Y line
    float sideDistX, sideDistY;     // Distance from player to the next grid X/Y line
    int mapX, mapY;                 // Current grid cell the ray is inside
    int stepX, stepY;               // Direction to step in grid (either +1 or -1)
    int side;                       // Hit a vertical (0) or horizontal (1) wall face?
} Ray;

//--- Player Structure Definition ---
typedef struct {
    float x, y, vx, vy, accel, max_accel, max_speed, angle, accel_growth;
} Player;

extern Player player;

//--- Main Rendering Function using DIB Section & 1D Map Array ---
void render_game(HWND hwnd, HDC hdc) {
    //--- 1. Get Window Client Dimensions ---
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) return;

    //--- 2. Setup DIB Section Header for Direct Pixel Buffer Access ---
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Negative height = top-down bitmap (0,0 at top-left)
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;     // 32 bits per pixel (BGRA)
    bmi.bmiHeader.biCompression = BI_RGB;

    void *pixelPixels = NULL;
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pixelPixels, NULL, 0);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Cast raw memory to an array of 32-bit integers (0x00RRGGBB)
    unsigned int *pixels = (unsigned int *)pixelPixels;

    //--- 3. Clear Background (Fill pixel buffer with dark industrial gray) ---
    for (int p = 0; p < width * height; p++) {
        pixels[p] = 0x00181818; 
    }

    //--- 4. Setup Camera Direction and Camera Plane Vectors ---
    float dirX = cosf(player.angle);
    float dirY = sinf(player.angle);
    float plane_x = -dirY * 0.66f; // Field of view plane X
    float plane_y = dirX * 0.66f;  // Field of view plane Y

    //--- 5. Raycasting Loop (Cast One Ray Per Screen Column) ---
    for (int i = 0; i < width; i++) {
        Ray ray;

        float camera_x = 2.0f * i / (float)width - 1.0f;
        ray.rayDirX = dirX + plane_x * camera_x;
        ray.rayDirY = dirY + plane_y * camera_x;

        float playerX_grid = player.x / (float)GRID_SIZE;
        float playerY_grid = player.y / (float)GRID_SIZE;

        ray.mapX = (int)playerX_grid;
        ray.mapY = (int)playerY_grid;

        ray.deltaDistX = (ray.rayDirX == 0) ? 1e30f : fabsf(1.0f / ray.rayDirX);
        ray.deltaDistY = (ray.rayDirY == 0) ? 1e30f : fabsf(1.0f / ray.rayDirY);

        if (ray.rayDirX < 0) {
            ray.stepX = -1;
            ray.sideDistX = (playerX_grid - ray.mapX) * ray.deltaDistX;
        } else {
            ray.stepX = 1;
            ray.sideDistX = (ray.mapX + 1.0f - playerX_grid) * ray.deltaDistX;
        }

        if (ray.rayDirY < 0) {
            ray.stepY = -1;
            ray.sideDistY = (playerY_grid - ray.mapY) * ray.deltaDistY;
        } else {
            ray.stepY = 1;
            ray.sideDistY = (ray.mapY + 1.0f - playerY_grid) * ray.deltaDistY;
        }

        //--- DDA Loop ---
        int hit = 0;
        int side = 0;
        int hitPillar = 0;
        float pillarDist = 0.0f;
        int pillarSide = 0;

        float playerGridX = player.x / (float)GRID_SIZE;
        float playerGridY = player.y / (float)GRID_SIZE;

        while (hit == 0) {
            if (ray.sideDistX < ray.sideDistY) {
                ray.sideDistX += ray.deltaDistX;
                ray.mapX += ray.stepX;
                side = 0;
            } else {
                ray.sideDistY += ray.deltaDistY;
                ray.mapY += ray.stepY;
                side = 1;
            }

            if (ray.mapX < 0 || ray.mapX >= WORLD_WIDTH || ray.mapY < 0 || ray.mapY >= WORLD_HEIGHT) {
                hit = 1;
            } else {
                // --- 1D Map Array Lookup Formula ---
                int tileID = grid[ray.mapY * WORLD_WIDTH + ray.mapX];

                if (tileID == 1 || tileID == 4) {
                    hit = 1;
                } else if (tileID == 3) {
                    if (check_pillar_intersection(&ray, ray.mapX, ray.mapY, playerGridX, playerGridY, &pillarDist, &pillarSide)) {
                        hit = 1;
                        hitPillar = 1;
                    }
                } else if (tileID == 2) {
                    if (check_door_left_intersection(&ray, ray.mapX, ray.mapY, playerGridX, playerGridY, &pillarDist, &pillarSide)) {
                        hit = 1;
                        hitPillar = 1;
                    }
                }
            }
        }

        //--- Retrieve Tile ID from 1D Map Array ---
        int tileID = 1;
        if (ray.mapX >= 0 && ray.mapX < WORLD_WIDTH && ray.mapY >= 0 && ray.mapY < WORLD_HEIGHT) {
            tileID = grid[ray.mapY * WORLD_WIDTH + ray.mapX];
            if (tileID <= 0) tileID = 1;
        }

        //--- Calculate Perpendicular Distance ---
        float perpWallDist;
        if (hitPillar) {
            perpWallDist = pillarDist;
            side = pillarSide;
        } else {
            if (side == 0) {
                perpWallDist = (ray.sideDistX - ray.deltaDistX);
            } else {
                perpWallDist = (ray.sideDistY - ray.deltaDistY);
            }
        }

        //--- Calculate Height of Wall Strip ---
        int lineHeight = (int)(height / perpWallDist);
        if (tileID == 3) {
            lineHeight = (int)(lineHeight * 1.8f);
        }

        int drawStart = -lineHeight / 2 + height / 2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + height / 2;
        if (drawEnd >= height) drawEnd = height - 1;

        //--- Select Hex Color (Hex format: 0x00BBGGRR) ---
        unsigned int wallColor;
        if (tileID == 2) {
            wallColor = (side == 1) ? 0x00237E23 : 0x0032CD32; // Green shades
        } else if (tileID == 3) {
            wallColor = (side == 1) ? 0x003232CD : 0x004169E1; // Blue shades
        } else {
            wallColor = (side == 1) ? 0x003232CD : 0x00CD5C5C; // Red shades
        }

        //--- Draw Vertical Column Directly into Pixel Buffer ---
        for (int y = drawStart; y <= drawEnd; y++) {
            pixels[y * width + i] = wallColor;
        }
    }

    //--- 6. Blit Finished Pixel Buffer to Screen ---
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    //--- 7. Clean Up DIB Resources ---
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}
//--- Helper Function: Analytic ray-box intersection for pillars ---
int check_pillar_intersection(Ray *ray, int mapX, int mapY, float playerGridX, float playerGridY, float *outDist, int *outSide) {
    float boxMinX = mapX + 0.35f;
    float boxMaxX = mapX + 0.65f;
    float boxMinY = mapY + 0.35f;
    float boxMaxY = mapY + 0.65f;

    float invDirX = (ray->rayDirX == 0.0f) ? 1e30f : (1.0f / ray->rayDirX);
    float invDirY = (ray->rayDirY == 0.0f) ? 1e30f : (1.0f / ray->rayDirY);

    float t1 = (boxMinX - playerGridX) * invDirX;
    float t2 = (boxMaxX - playerGridX) * invDirX;
    float tmin = fminf(t1, t2);
    float tmax = fmaxf(t1, t2);

    float ty1 = (boxMinY - playerGridY) * invDirY;
    float ty2 = (boxMaxY - playerGridY) * invDirY;
    float tymin = fminf(ty1, ty2);
    float tymax = fmaxf(ty1, ty2);

    float tEnter = fmaxf(tmin, tymin);
    float tExit = fminf(tmax, tymax);

    if (tEnter <= tExit && tExit > 0.0f) {
        *outDist = (tEnter > 0.0f) ? tEnter : tExit;
        *outSide = (tmin > tymin) ? 0 : 1; // 0 for vertical, 1 for horizontal
        return 1; // Hit!
    }
    return 0; // Miss!
}

int check_door_left_intersection(Ray *ray, int mapX, int mapY, float playerGridX, float playerGridY, float *outDist, int *outSide) {
    float boxMinX = mapX + 0.35f;
    float boxMaxX = mapX + 0.65f;
    float boxMinY = mapY + 0.0f;
    float boxMaxY = mapY + 1.0f;

    float invDirX = (ray->rayDirX == 0.0f) ? 1e30f : (1.0f / ray->rayDirX);
    float invDirY = (ray->rayDirY == 0.0f) ? 1e30f : (1.0f / ray->rayDirY);

    float t1 = (boxMinX - playerGridX) * invDirX;
    float t2 = (boxMaxX - playerGridX) * invDirX;
    float tmin = fminf(t1, t2);
    float tmax = fmaxf(t1, t2);

    float ty1 = (boxMinY - playerGridY) * invDirY;
    float ty2 = (boxMaxY - playerGridY) * invDirY;
    float tymin = fminf(ty1, ty2);
    float tymax = fmaxf(ty1, ty2);

    float tEnter = fmaxf(tmin, tymin);
    float tExit = fminf(tmax, tymax);

    if (tEnter <= tExit && tExit > 0.0f) {
        *outDist = (tEnter > 0.0f) ? tEnter : tExit;
        *outSide = (tmin > tymin) ? 0 : 1; // 0 for vertical, 1 for horizontal
        return 1; // Hit!
    }
    return 0; // Miss!
}