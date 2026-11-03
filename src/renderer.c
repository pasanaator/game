#define WIN32_LEAN_AND_MEAN
#include "renderer.h"
#include <math.h>
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
//--- External References to Map & Player ---

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

extern uint8_t mapWidth;
extern uint8_t mapHeight;
extern const int TILE_SIZE;
extern MapTile *ActiveMap;
extern uint32_t *wallTexture;
extern uint32_t *BrickwallTexture;
extern uint32_t *VentTexture;
extern uint32_t *FloorTexture;
extern uint32_t *CeillingTexture;

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
    float speed, currentSpeed, acceleration, maxAcceleration, accelerationGrowth, decelerationRate, dirY, dirX;
} Movement;

typedef struct {
    float x, y;
    float angle;
    Movement movement;
} Player;

extern Player player;

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

//--- Main Rendering Function using DIB Section & 1D Map Array ---
void render_game(DWORD *pixels, int width, int height) {
    if (width <= 0 || height <= 0) return;

    // --- Safety Guard: Ensure map and dimensions are valid ---
    if (!ActiveMap || mapWidth == 0 || mapHeight == 0) {
        for (int p = 0; p < width * height; p++) {
            pixels[p] = 0x00181818;
        }
        return;
    }

    //--- 1. Clear Background (Fill pixel buffer with dark industrial gray) ---
    for (int p = 0; p < width * height; p++) {
        pixels[p] = 0x00181818; 
    }

    //--- 2. Setup Camera Direction and Camera Plane Vectors ---
    float dirX = cosf(player.angle);
    float dirY = sinf(player.angle);
    float plane_x = -dirY * 0.66f; 
    float plane_y = dirX * 0.66f;  

    float playerGridX = player.x / (float)TILE_SIZE;
    float playerGridY = player.y / (float)TILE_SIZE;

    //--- 3. Raycasting Loop (Cast One Ray Per Screen Column) ---
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

        //--- DDA Loop ---
        int hit = 0;
        int side = 0;
        int hitPillar = 0;
        float pillarDist = 0.0f;
        int pillarSide = 0;

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

            // Strict bounds check before any array indexing
            if (ray.mapX < 0 || ray.mapX >= (int)mapWidth || ray.mapY < 0 || ray.mapY >= (int)mapHeight) {
                hit = 1;
            } else {
                int tileID = ActiveMap[ray.mapY * mapWidth + ray.mapX].tile;

                if (tileID == 4 || tileID == 1 || tileID == 2 || tileID == 5) {
                    hit = 1;
                } else if (tileID == 3) {
                    if (check_pillar_intersection(&ray, ray.mapX, ray.mapY, playerGridX, playerGridY, &pillarDist, &pillarSide)) {
                        hit = 1;
                        hitPillar = 1;
                    }
                } else if (tileID == 5) {
                    if (check_door_left_intersection(&ray, ray.mapX, ray.mapY, playerGridX, playerGridY, &pillarDist, &pillarSide)) {
                        hit = 1;
                        hitPillar = 1;
                    }
                }
            }
        }

        //--- Retrieve Tile ID Safely ---
        int tileID = 4;
        if (ray.mapX >= 0 && ray.mapX < (int)mapWidth && ray.mapY >= 0 && ray.mapY < (int)mapHeight) {
            tileID = ActiveMap[ray.mapY * mapWidth + ray.mapX].tile;
            if (tileID <= 0) tileID = 4;
        }

        //--- Calculate Perpendicular Distance (Fixes Fisheye Warp) ---
        float perpWallDist;
        if (hitPillar) {
            perpWallDist = pillarDist * (ray.rayDirX * dirX + ray.rayDirY * dirY);
            side = pillarSide;
        } else {
            perpWallDist = (side == 0) ? (ray.sideDistX - ray.deltaDistX) : (ray.sideDistY - ray.deltaDistY);
        }

        if (perpWallDist < 1e-4f) perpWallDist = 1e-4f;

        //--- Calculate Height of Wall Strip ---
        int lineHeight = (int)(height / perpWallDist);

        // Keep true unclipped start position for smooth close-up rendering
        int realDrawStart = -lineHeight / 2 + height / 2;

        int drawStart = realDrawStart;
        if (drawStart < 0) drawStart = 0;
        
        int drawEnd = lineHeight / 2 + height / 2;
        if (drawEnd >= height) drawEnd = height - 1;

        //--- Calculate Texture Coordinates ---
        float wallX = 0.0f;
        if (hitPillar) {
            // Must use raw Euclidean pillarDist for exact world hit coordinates
            float hitX = playerGridX + pillarDist * ray.rayDirX;
            float hitY = playerGridY + pillarDist * ray.rayDirY;

            if (tileID == 3) { 
                if (side == 0) {
                    wallX = (hitY - (ray.mapY + 0.35f)) / 0.30f;
                } else {
                    wallX = (hitX - (ray.mapX + 0.35f)) / 0.30f;
                }
            } else if (tileID == 5) {
                if (side == 0) {
                    wallX = hitY - floorf(hitY);
                } else {
                    wallX = (hitX - (ray.mapX + 0.35f)) / 0.30f;
                }
            }
        } else {
            if (side == 0) {
                wallX = playerGridY + perpWallDist * ray.rayDirY;
            } else {
                wallX = playerGridX + perpWallDist * ray.rayDirX;
            }
            wallX -= floorf(wallX); // Keep decimal fraction (0.0 to 1.0)
        }

        if (wallX < 0.0f) wallX = 0.0f;
        if (wallX > 1.0f) wallX = 1.0f;

        // Map to 128x128 texture dimensions
        int texX = (int)(wallX * 128.0f);
        if (texX >= 128) texX = 127;
        
        if (side == 0 && ray.rayDirX > 0) texX = 128 - texX - 1;
        if (side == 1 && ray.rayDirY < 0) texX = 128 - texX - 1;

        //--- Draw Vertical Column Using Texture Sampling ---
        for (int y = drawStart; y <= drawEnd; y++) {
            int texY = (y - realDrawStart) * 128 / lineHeight;
            if (texY < 0) texY = 0;
            if (texY >= 128) texY = 127;

            uint32_t color = 0x00FF00FF; // Magenta fallback error color

            if (tileID == 3) {
                if (BrickwallTexture) {
                    color = BrickwallTexture[texY * 128 + texX];
                }
            } else if (tileID == 4) {
                if (wallTexture) {
                    color = wallTexture[texY * 128 + texX];
                }
            } else if (tileID == 1) {
                color = 0x00AF3232;
            } else if (tileID == 2) {
                color = 0x00CDCD23;
            } else if (tileID == 5) {
                if (VentTexture) {
                    color = VentTexture[texY * 128 + texX];
                }
            }
            
            // Optional side-shading depth effect for horizontal/vertical walls
            if (side == 1) {
                color = (color >> 1) & 0x007F7F7F;
            }

            pixels[y * width + i] = color;
        }
        for (int y = drawEnd + 1; y < height; y++) {
            float rowDistance = (0.5f * (float)height) / ((float)y - (float)height * 0.5f);

            float floorX = playerGridX + rowDistance * ray.rayDirX;
            float floorY = playerGridY + rowDistance * ray.rayDirY;

            int floorTexX = (int)(128.0f * (floorX - floorf(floorX))) & 127;
            int floorTexY = (int)(128.0f * (floorY - floorf(floorY))) & 127;

            uint32_t color = 0x00282828;
            if(FloorTexture) {
                color = FloorTexture[floorTexY * 128 + floorTexX];
            }
            color = (color >> 1) & 0x007F7F7F;

            pixels[y * width + i] = color;
        }
        for (int y = 0; y < drawStart; y++) {
            
            float rowDistance = (0.5f * (float)height) / ((float)height * 0.5f - (float)y);

            float ceillingX = playerGridX + rowDistance * ray.rayDirX;
            float ceillingY = playerGridY + rowDistance * ray.rayDirY;

            int mapX = (int)floorf(ceillingX);
            int mapY = (int)floorf(ceillingY);

            uint32_t color = 0x00181818;

            if (mapX >= 0 && mapX < (int)mapWidth && mapY >= 0 && mapY < (int)mapHeight) {
                int ceillingTileID = ActiveMap[mapY * mapWidth + mapX].tile;

                int texX = (int)(128.0f * (ceillingX - floorf(ceillingX))) & 127;
                int texY = (int)(128.0f * (ceillingY - floorf(ceillingY))) & 127;

                
                if (CeillingTexture) {
                    color = CeillingTexture[texY * 128 + texX];
                }


            }
            pixels[y * width + i] = color;
        }
    }
}