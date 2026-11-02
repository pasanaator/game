#include <stdio.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define MAP_VERSION 1
#define MAP_HEIGHT 12
#define MAP_WIDTH  9
#define MAX_TILES  (MAP_WIDTH * MAP_HEIGHT)

#pragma pack(push, 1)

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

#pragma pack(pop)

#define _  { .tile = 0, .isDeadly = false, .isLevelTransition = false }
#define W  { .tile = 1, .isDeadly = false, .isLevelTransition = false }
#define K  { .tile = 2, .isDeadly = true, .isLevelTransition = false }
#define P  { .tile = 4, .isDeadly = false, .isLevelTransition = false }

static const MapTile maps[][MAX_TILES] = {
    {
        W, W, W, W, W, W, W, W, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, P, _, P, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, P, _, P, _, P, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, P, _, P, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, P, _, P, _, P, _, W,
        W, _, _, _, _, _, _, _, W,
        W, W, W, W, W, W, W, W, W,
    },
    {
        W, W, W, W, W, W, W, W, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, W, W, _, W, W, _, W,
        W, _, W, _, _, _, W, _, W,
        W, _, W, _, _, _, W, _, W,
        W, _, W, _, _, _, W, _, W,
        W, _, W, _, _, _, W, _, W,
        W, _, W, W, W, W, W, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, K, K, K, _, _, W,
        W, W, W, W, W, W, W, W, W,
    },
    {
        W, W, W, W, W, W, W, W, W,
        W, _, W, _, _, _, _, _, W,
        W, _, W, _, W, W, W, _, W,
        W, _, W, _, W, _, W, _, W,
        W, _, _, _, W, _, W, _, W,
        W, W, W, _, W, _, _, _, W,
        W, _, _, _, W, _, W, W, W,
        W, _, W, W, W, _, _, _, W,
        W, _, _, _, W, W, W, _, W,
        W, W, W, _, W, _, W, _, W,
        W, _, _, _, _, _, W, _, W,
        W, W, W, W, W, W, W, W, W,
    },
    {
        W, W, W, W, W, W, W, W, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, W, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, K, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, P, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, W, W, W, W, W, W, W, W,
    },
    {
        W, W, W, W, W, W, W, W, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, _, _, _, _, _, _, _, W,
        W, W, W, W, W, W, W, W, W,
    }
};

#undef _
#undef W

#define AVAILABLE_LEVELS (sizeof(maps) / sizeof(maps[0]))

bool SaveMapsToFile(const char *filepath, const MapTile *map_data, uint16_t num_levels) {
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        perror("Failed to open file for writing");
        return false;
    }

    MapHeader header = {
        .version = MAP_VERSION,
        .width = MAP_WIDTH,
        .height = MAP_HEIGHT,
        .total_levels = (uint8_t)num_levels,
        .pad = 0
    };

    if (fwrite(&header, sizeof(MapHeader), 1, fp) != 1) {
        perror("Failed to write header");
        fclose(fp);
        return false;
    }

    size_t total_tiles = (size_t)num_levels * MAX_TILES;
    size_t written = fwrite(map_data, sizeof(MapTile), total_tiles, fp);

    fclose(fp);
    return written == total_tiles;
}

int main(void) {
    const char *filepath = "test.gmap"; // Adjusted relative path if needed
    uint16_t num_levels = (uint16_t)AVAILABLE_LEVELS;

    printf("Welcome to the map creation tool!\n");
    printf("Map Version: %d\n", MAP_VERSION);
    printf("Available levels in template: %u\n", num_levels);
    printf("Map Height: %d  Map Width: %d\n", MAP_HEIGHT, MAP_WIDTH);
    printf("Writing map to '%s'...\n", filepath);
    Sleep(1000);
    if (SaveMapsToFile(filepath, (const MapTile *)maps, num_levels)) {
        printf("Map successfully written to '%s'!\n", filepath);
    } else {
        printf("Failed to write map file.\n");
    }

    return 0;
}