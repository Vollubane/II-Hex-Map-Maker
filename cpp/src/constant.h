#pragma once

// std include
#include <map>
#include <string>
#include <unordered_map>

const int FILE_MANIPULATION_MAX_DEEP = 7;
const float HEX_GRID_DIAMETER = 1.8f;
const float HEX_TILES_ANIMATION_TIME = 1.0f;
const int FPS_IN_CALCULATION = 60;

const enum class E_ManifestJsonKey // JSON field names: manifest (assets_data row keys, sidecar dicts, etc.)
{
    BIN,
    TEXTURE,
    TRUE_PATH,
    WEIGHT,
    GROUP
};

const std::map<E_ManifestJsonKey, std::string> M_MANIFEST_JSON_KEY_STRINGS // String keys as in manifeste.json
{
    { E_ManifestJsonKey::BIN, "bin" },
    { E_ManifestJsonKey::TEXTURE, "texture" },
    { E_ManifestJsonKey::TRUE_PATH, "true_path" },
    { E_ManifestJsonKey::WEIGHT, "weight" },
    { E_ManifestJsonKey::GROUP, "group" }
};

const enum class E_CameraMapMovementType // Type of movement for the cameraMap.
{
    CAMERA_MAP_CLASSIC_MODE,
    CAMERA_MAP_TOPDOWN_MODE,
    CAMERA_MAP_MOUSE_MODE
};

const std::unordered_map<std::string, std::string> M_ModulesList // List of all Modules and sub module with a default scenes.
{
    { "CAMERA_MAP", "res://Scene/Module/CameraMap.tscn" },
    { "TILES_CONTAINER", "res://Scene/Module/TilesContainer.tscn" },
    { "TILE_3D", "res://Scene/Module/Tile3D.tscn" },
    { "IMPORTER_PICTURE_MAKER", "res://Scene/Module/ImporterPictureMaker.tscn"}
};
