#pragma once

// Custom include
#include "constant.h"

// Godot include
#include <godot_cpp/classes/node3d.hpp>

using namespace godot;

namespace TileModule {
    class Tile3D : public Node3D {
        GDCLASS(Tile3D, Node3D)

        private :

        protected:
            /**
             * @brief Binds methods exposed to Godot.
            **/
            static void _bind_methods();

        public:

            /**
             * @brief Constructor of the node.
            **/
            Tile3D();

            /**
             * @brief Destructor.
            **/
            ~Tile3D();
    };
}
