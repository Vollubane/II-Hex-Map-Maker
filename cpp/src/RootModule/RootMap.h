#pragma once

// Custom include
#include "constant.h"

// Godot include
#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace RootModule {
    class RootMap : public Node {
        GDCLASS(RootMap, Node)

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
            RootMap();

            /**
             * @brief Destructor.
            **/
            ~RootMap();
    };
}
