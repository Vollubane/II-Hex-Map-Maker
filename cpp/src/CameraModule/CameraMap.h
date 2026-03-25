#pragma once

// Godot include
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/core/class_db.hpp>


namespace godot {

class CameraMap : public Camera3D {
    GDCLASS(CameraMap, Camera3D)

protected:
    static void _bind_methods();

public:
    CameraMap();
    ~CameraMap();
};

} // namespace godot
