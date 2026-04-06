#pragma once

// Custom include
#include "constant.h"

// Godot include
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/tween.hpp>
#include <godot_cpp/classes/tweener.hpp>
#include <godot_cpp/classes/property_tweener.hpp>

using namespace godot;

namespace TileModule {
    /**
     * @brief Visual representation of one hex cell on the map.
     *
     * Positions the tile in world space from axial map coordinates and plays
     * scale tweens when the tile appears or is recycled. Tile-specific data can
     * be attached through the dictionary for gameplay or rendering extensions.
     */
    class Tile3D : public Node3D {
        GDCLASS(Tile3D, Node3D)

        private :

            Dictionary m_data;      //!< Tile data.
            Ref<Tween> m_tween;     //!< Active tween for spawn/despawn animation.

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

            /**
             * @brief Called when the node enters the scene tree.
            **/
            void _ready();

            /**
             * @brief Places the tile at the given axial map position and plays the appear animation.
             * @param p_mapPosition Axial coordinates (q, r) on the hex grid.
             * @param p_tileData Optional dictionary stored as this tile's payload (see m_data).
            **/
            void loadAtPosition(const Vector2i p_mapPosition, const Dictionary p_tileData = {});

            /**
             * @brief Starts the disappear animation before the tile is hidden or pooled.
             * @details On tween completion, visibility is turned off via @ref onDisappearFinished.
            **/
            void unload();
        private:

            /**
             * @brief Called when the unload tween finishes.
             * @details Hides the node so a pooled instance can be reused safely.
            **/
            void onDisappearFinished();

            /**
             * @brief Converts axial hex coordinates to a world-space position on the ground plane.
             * @param p_mapPosition Axial coordinates (q, r) matching the grid layout used by the map.
             * @return Local/world XZ position (Y left to the scene setup).
            **/
            const Vector3 getWorldPositionFromMapPosition(const Vector2i p_mapPosition);

    };
}
