#pragma once

// std include
#include <string>
#include <unordered_map>

const enum class E_CameraMapMovementType // Type of movement for the cameraMap.
{
    CAMERA_MAP_CLASSIC_MODE,
    CAMERA_MAP_TOPDOWN_MODE,
    CAMERA_MAP_MOUSE_MODE
};

const std::unordered_map<std::string, std::string> M_ModulesList // List of all Modules and their default scenes.
{
    { "CAMERA_MAP", "res://Scene/Module/CameraMap.tscn" },
    { "TILES_CONTAINER", "res://Scene/Module/TilesContainer.tscn" },
    { "TILE_3D", "res://Scene/Module/Tile3D.tscn" }
};
