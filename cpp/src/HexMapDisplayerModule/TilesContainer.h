#pragma once

// Custom include
#include "constant.h"

// Godot include
#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace HMDisplayerModule {
    class TilesContainer : public Node {
        GDCLASS(TilesContainer, Node)

        private :

            Node* m_hexMapEventBus; //!< Pointer to the scene event bus used to receive and emit HexMap signals.

        protected:
            /**
             * @brief Binds methods exposed to Godot.
            **/
            static void _bind_methods();

        public:

            /**
             * @brief Constructor of the node.
            **/
            TilesContainer();

            /**
             * @brief Destructor.
            **/
            ~TilesContainer();

            /**
             * @brief Called when the node enters the scene tree.
            **/
            void _ready();
    };
}
