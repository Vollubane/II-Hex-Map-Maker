#pragma once

// Custom include
#include "constant.h"

// Godot include
#include <godot_cpp/classes/node3d.hpp>

using namespace godot;

namespace RootModule {

    class RootTilesPack : public Node3D {
        GDCLASS(RootTilesPack, Node3D)

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
            RootTilesPack();

            /**
             * @brief Destructor.
            **/
            ~RootTilesPack();


        private:

    };
}
