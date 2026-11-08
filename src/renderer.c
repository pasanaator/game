#define WIN32_LEAN_AND_MEAN
#include "renderer.h"
#include <math.h>
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

//--- Struct Definitions ---
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
    float rayDirX, rayDirY;         
    float deltaDistX, deltaDistY;   
    float sideDistX, sideDistY;     
    int mapX, mapY;                 
    int stepX, stepY;               
    int side;                       
} Ray;

typedef struct {
    float speed, currentSpeed, acceleration, maxAcceleration, accelerationGrowth, decelerationRate, dirY, dirX;
} Movement;

typedef struct {
    float x, y, stX, stY;
    float angle;
    Movement movement;
} Player;

//--- External References ---
extern uint8_t mapWidth;
extern uint8_t mapHeight;
extern const int TILE_SIZE;
extern MapTile *ActiveMap;
extern uint32_t *VentTexture;
extern uint32_t *wallTexture;
extern uint32_t *BrickwallTexture;
extern uint32_t *FloorTexture;
extern uint32_t *CeilingTexture;
extern Player player;

//--- Fog Utility ---
uint32_t apply_fog(uint32_t color, float distance, float maxDistance) {
    float fogFactor = distance / maxDistance;
    if (fogFactor > 1.0f) fogFactor = 1.0f;
    if (fogFactor < 0.0f) fogFactor = 0.0f;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    uint8_t fogR = 0x18, fogG = 0x18, fogB = 0x18;

    r = (uint8_t)(r * (1.0f - fogFactor) + fogR * fogFactor);
    g = (uint8_t)(g * (1.0f - fogFactor) + fogG * fogFactor);
    b = (uint8_t)(b * (1.0f - fogFactor) + fogB * fogFactor);

    return (r << 16) | (g << 8) | b;
}

int intersect_box(Ray *ray, float playerGridX, float playerGridY, 
                  float minX, float maxX, float minY, float maxY, 
                  float *outDist, int *outSide) {
    float invDirX = (ray->rayDirX == 0.0f) ? 1e30f : (1.0f / ray->rayDirX);
    float invDirY = (ray->rayDirY == 0.0f) ? 1e30f : (1.0f / ray->rayDirY);

    float t1 = (minX - playerGridX) * invDirX;
    float t2 = (maxX - playerGridX) * invDirX;
    float tmin = fminf(t1, t2);
    float tmax = fmaxf(t1, t2);

    float ty1 = (minY - playerGridY) * invDirY;
    float ty2 = (maxY - playerGridY) * invDirY;
    float tymin = fminf(ty1, ty2);
    float tymax = fmaxf(ty1, ty2);

    float tEnter = fmaxf(tmin, tymin);
    float tExit = fminf(tmax, tymax);

    if (tEnter <= tExit && tExit > 0.0f) {
        *outDist = (tEnter > 0.0f) ? tEnter : tExit;
        *outSide = (tmin > tymin) ? 0 : 1; // 0 for vertical, 1 for horizontal
        return 1;
    }
    return 0;
}

int check_door_vertical(Ray *ray, int mapX, int mapY, float playerGridX, float playerGridY, float *outDist, int *outSide, int *outTexType) {
    float closestDist = 1e30f;
    int hitAnything = 0;
    float tempDist;
    int tempSide;

    if (intersect_box(ray, playerGridX, playerGridY, mapX + 0.0f, mapX + 0.2f, mapY + 0.4f, mapY + 0.6f, &tempDist, &tempSide)) {
        if (tempDist < closestDist) {
            closestDist = tempDist;
            *outSide = tempSide;
            *outTexType = 0;
            hitAnything = 1;
        }
    }
    if (intersect_box(ray, playerGridX, playerGridY, mapX + 0.8f, mapX + 1.0f, mapY + 0.4f, mapY + 0.6f, &tempDist, &tempSide)) {
        if (tempDist < closestDist) {
            closestDist = tempDist;
            *outSide = tempSide;
            *outTexType = 0;
            hitAnything = 1;
        }
    }
    if (intersect_box(ray, playerGridX, playerGridY, mapX + 0.2f, mapX + 0.8f, mapY + 0.45f, mapY + 0.55f, &tempDist, &tempSide)) {
        if (tempDist < closestDist) {
            closestDist = tempDist;
            *outSide = tempSide;
            *outTexType = 1;
            hitAnything = 1;
        }
    }

    if (hitAnything) {
        *outDist = closestDist;
        return 1; // Successfully hit part of the door structure!
    }

    return 0; // Missed all parts (ray passes through the open gap)
}
//--- Core Rendering Function ---
void render_game(DWORD *pixels, int width, int height) {
    if (width <= 0 || height <= 0) return;

    if (!ActiveMap || mapWidth == 0 || mapHeight == 0) {
        for (int p = 0; p < width * height; p++) pixels[p] = 0x00181818;
        return;
    }

    // Clear Screen Buffer
    for (int p = 0; p < width * height; p++) {
        pixels[p] = 0x00181818; 
    }

    float dirX = cosf(player.angle);
    float dirY = sinf(player.angle);
    float plane_x = -dirY * 0.66f; 
    float plane_y = dirX * 0.66f;  

    float playerGridX = player.x / (float)TILE_SIZE;
    float playerGridY = player.y / (float)TILE_SIZE;

    // Raycast Column Loop
    for (int i = 0; i < width; i++) {
        Ray ray;
        float camera_x = 2.0f * i / (float)width - 1.0f;
        ray.rayDirX = dirX + plane_x * camera_x;
        ray.rayDirY = dirY + plane_y * camera_x;

        ray.mapX = (int)playerGridX;
        ray.mapY = (int)playerGridY;

        ray.deltaDistX = (ray.rayDirX == 0) ? 1e30f : fabsf(1.0f / ray.rayDirX);
        ray.deltaDistY = (ray.rayDirY == 0) ? 1e30f : fabsf(1.0f / ray.rayDirY);

        if (ray.rayDirX < 0) {
            ray.stepX = -1;
            ray.sideDistX = (playerGridX - ray.mapX) * ray.deltaDistX;
        } else {
            ray.stepX = 1;
            ray.sideDistX = (ray.mapX + 1.0f - playerGridX) * ray.deltaDistX;
        }

        if (ray.rayDirY < 0) {
            ray.stepY = -1;
            ray.sideDistY = (playerGridY - ray.mapY) * ray.deltaDistY;
        } else {
            ray.stepY = 1;
            ray.sideDistY = (ray.mapY + 1.0f - playerGridY) * ray.deltaDistY;
        }

        // --- DDA Loop ---
        int hit = 0;
        int side = 0;
        float doorDist = 0.0f;
        int doorTexType = 0;
        int hitDoor = 0;

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

            if (ray.mapX < 0 || ray.mapX >= (int)mapWidth || ray.mapY < 0 || ray.mapY >= (int)mapHeight) {
                hit = 1;
            } else {
                int tileID = ActiveMap[ray.mapY * mapWidth + ray.mapX].tile;

                if (tileID == 4 || tileID == 1 || tileID == 2) {
                    hit = 1; 
                } 
                else if (tileID == 3) {
                    // Test ray against our new 3-part door model!
                    if (check_door_vertical(&ray, ray.mapX, ray.mapY, playerGridX, playerGridY, &doorDist, &side, &doorTexType)) {
                        hit = 1;
                        hitDoor = 1;
                    }
                }
            }
        }

        int tileID = 4;
        if (ray.mapX >= 0 && ray.mapX < (int)mapWidth && ray.mapY >= 0 && ray.mapY < (int)mapHeight) {
            tileID = ActiveMap[ray.mapY * mapWidth + ray.mapX].tile;
            if (tileID <= 0) tileID = 4;
        }
        float perpWallDist;
        if (hitDoor) {
            perpWallDist = doorDist * (ray.rayDirX * dirX + ray.rayDirY * dirY);
        } else {
            perpWallDist = (side == 0) ? (ray.sideDistX - ray.deltaDistX) : (ray.sideDistY - ray.deltaDistY);
        }

        if (perpWallDist < 1e-4f) perpWallDist = 1e-4f;

        // Distance & Line Calculation
        if (perpWallDist < 1e-4f) perpWallDist = 1e-4f;

        int lineHeight = (int)(height / perpWallDist);
        int realDrawStart = -lineHeight / 2 + height / 2;
        int drawStart = (realDrawStart < 0) ? 0 : realDrawStart;
        int drawEnd = (lineHeight / 2 + height / 2 >= height) ? height - 1 : lineHeight / 2 + height / 2;

        // Texture Mapping Coordinates
        float wallX = (side == 0) ? (playerGridY + perpWallDist * ray.rayDirY) : (playerGridX + perpWallDist * ray.rayDirX);
        wallX -= floorf(wallX);

        int texX = (int)(wallX * 128.0f);
        if (texX >= 128) texX = 127;
        if (side == 0 && ray.rayDirX > 0) texX = 128 - texX - 1;
        if (side == 1 && ray.rayDirY < 0) texX = 128 - texX - 1;

        // Draw Wall Column
        for (int y = drawStart; y <= drawEnd; y++) {
            int texY = (y - realDrawStart) * 128 / lineHeight;
            if (texY < 0) texY = 0;
            if (texY >= 128) texY = 127;

            uint32_t color = 0x00FF00FF; // Magenta error fallback

            // --- TEXTURE SELECTION LOGIC ---
            if (hitDoor) {
                if (doorTexType == 0) {
                    // Side Jambs use the standard wall texture
                    if (wallTexture) color = wallTexture[texY * 128 + texX];
                } else if (doorTexType == 1) {
                    // Center Door Panel uses the Vent/Door texture
                    if (BrickwallTexture) color = BrickwallTexture[texY * 128 + texX];
                }
            } else {
                // Standard walls (Tile 4, etc.)
                if (tileID == 4 && wallTexture) {
                    color = wallTexture[texY * 128 + texX];
                } else if (tileID == 1) {
                    color = 0x00AF3232; // Solid color fallbacks for other IDs if needed
                } else if (tileID == 2) {
                    color = 0x00CDCD23;
                }
            }
            
            // Side-shading depth effect & fog
            if (side == 1) color = (color >> 1) & 0x007F7F7F;
            color = apply_fog(color, perpWallDist, 7.0f);

            pixels[y * width + i] = color;
        }

        // Draw Floor
        for (int y = drawEnd + 1; y < height; y++) {
            float rowDistance = (0.5f * (float)height) / ((float)y - (float)height * 0.5f);
            float floorX = playerGridX + rowDistance * ray.rayDirX;
            float floorY = playerGridY + rowDistance * ray.rayDirY;

            int floorTexX = (int)(128.0f * (floorX - floorf(floorX))) & 127;
            int floorTexY = (int)(128.0f * (floorY - floorf(floorY))) & 127;

            uint32_t color = 0x00282828;
            if (FloorTexture) color = FloorTexture[floorTexY * 128 + floorTexX];
            color = (color >> 1) & 0x007F7F7F;

            pixels[y * width + i] = color;
        }

        for (int y = 0; y < drawStart; y++) {
            float rowDistance = (0.5f * (float)height) / ((float)height * 0.5f - (float)y);

            float ceilingX = playerGridX + rowDistance * ray.rayDirX;
            float ceilingY = playerGridY + rowDistance * ray.rayDirY;

            int ceilingTexX = (int)(128.0f * (ceilingX - floorf(ceilingX))) & 127;
            int ceilingTexY = (int)(128.0f * (ceilingY - floorf(ceilingY))) & 127;

            uint32_t color = 0x00181818;
            if (CeilingTexture) {
                color = CeilingTexture[ceilingTexY * 128 + ceilingTexX];
            }
            
            // Optional depth shading for ceilings, matching your floor style
            color = (color >> 1) & 0x007F7F7F;

            pixels[y * width + i] = color;
        }
    }
}