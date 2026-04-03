#pragma once

// Custom include
#include "constant.h"

// Godot include
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace RootModule {
    /**
     * @brief Orchestrates the loading and initialization of map modules.
     *
     * This node loads child module scenes asynchronously, instantiates them in
     * the scene tree, and emits the global scene-ready signal when every
     * required module reports that it is ready.
     */
    class RootMap : public Node3D {
        GDCLASS(RootMap, Node3D)

        private :

            Node* m_hexMapEventBus;             //!< Pointer to the scene event bus used to receive and emit HexMap signals.
            Array m_remainingModulesInThread;   //!< List of module names still waiting for threaded loading completion.
            Dictionary m_modulesReady;          //!< Map of module names and their ready state.
            bool m_isSceneInThread;             //!< Whether at least one module is still being loaded asynchronously.

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

            /**
             * @brief Called when the node enters the scene tree.
            **/
            void _ready();

            /**
             * @brief Polls the state of threaded module loading requests.
             * @param p_delta Elapsed time since the previous frame.
            **/
            void _process(double p_delta);

        private:

            /**
             * @brief Starts threaded loading for every pending module scene.
            **/
            void loadModulesInThread();

            /**
             * @brief Instantiates a loaded module scene and adds it to the scene tree.
             * @param p_module Loaded packed scene to instantiate.
             * @param p_moduleName Name of the instantiated module.
            **/
            void initModule(const Ref<PackedScene> p_module, const String p_moduleName);

            /**
             * @brief Emits the signal indicating that the full scene is ready.
            **/
            void emitSceneReady();

            /**
             * @brief Handles the ready notification of the tiles container module.
            **/
            void onTilesContainerReady();

            /**
             * @brief Handles the ready notification of the camera module.
            **/
            void onCameraMapReady();

            /**
             * @brief Gets the resource path of a module from its name.
             * @param p_moduleName Module identifier used in the module list.
             * @return The scene path associated with the requested module.
            **/
            const String getModulePath(const String &p_moduleName);
    };
}
