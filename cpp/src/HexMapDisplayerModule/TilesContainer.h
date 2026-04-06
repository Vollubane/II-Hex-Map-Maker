#pragma once

// Custom include
#include "constant.h"
#include "TileModule/Tile3D.h"

// Godot include
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>

using namespace godot;

namespace HMDisplayerModule {
    /**
     * @brief Manages a sliding window of hex tiles around the camera focus.
     *
     * Listens to the map event bus for the view center and zoom, keeps only
     * tiles inside an axial hex disk, and spreads instantiation, pooling, load
     * and unload across frames using a time budget so large maps stay responsive.
     */
    class TilesContainer : public Node {
        GDCLASS(TilesContainer, Node)

        private :

            Node* m_hexMapEventBus;                 //!< Pointer to the scene event bus used to receive and emit HexMap signals.
            Ref<PackedScene> m_tileScene;           //!< Packed scene used to instantiate Tile3D tile nodes.

            Dictionary m_coordonateOfMappedTiles;   //!< Map of tile coordinates to the active Tile3D node instance.
            Array m_coordonateOfTilesToUnload;      //!< Queue of tile coordinates pending to unload.
            Array m_coordonateOfTilesToLoad;        //!< Queue of tile coordinates pending to load.
            int m_mapRadius;                        //!< Radius of the visible disk.

            Array m_pooledTiles;                    //!< Pool of iddle Tile3D nodes.
            Vector2i m_focussedTile;                //!< Coordinates of the tile view center.

            int m_pendingTileToPool;                //!< Number of tile remaing to pool at start.
            Array m_pendingTilePositions;           //!< Positions of tile remaing to spawn at start.
            int m_pendingPackSize;                  //!< Batch size for timed load/unload steps.

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

            /**
             * @brief Processes timed unload and load batches for tile nodes.
             * @param p_delta Elapsed time since the previous frame (unused; work is budgeted in microseconds).
             * @details Adjusts @ref m_pendingPackSize from measured cost to stay within a frame budget.
            **/
            void _process(double p_delta);

        private:

            /**
             * @brief Reacts to a new map-space focus from the camera / event bus.
             * @param p_focusPoint Map plane position corresponding to the view center.
            **/
            void onFocusPointChanged(const Vector2 p_focusPoint);

            /**
             * @brief Updates the visible hex radius from the normalized zoom ratio.
             * @param p_zoomRatio Normalized zoom ratio in the 0–1 range; selects the axial map radius between the configured min and max.
            **/
            void onZoomChanged(const float p_zoomRatio);

            /**
             * @brief Preallocates pooled tiles and/or spawns initial tiles within a time budget.
             * @param p_remainingTileToPool Number of Tile3D instances still to instantiate into the pool.
             * @param p_remainingTilePositionToSpawn Axial positions to fill after pooling is complete.
             * @param p_packSize How many operations to run per inner loop before measuring time.
             * @details Emits @c tilesContainer_ready on the event bus when both pool and spawn lists are done.
            **/
            void loadTilesContainer(const int p_remainingTileToPool, const Array p_remainingTilePositionToSpawn, const int p_packSize = 10);

            /**
             * @brief Recomputes which tiles should be visible and fills load/unload queues.
             * @details Compares the desired axial disk with @ref m_coordonateOfMappedTiles.
            **/
            void updateMap();


            /**
             * @brief Defers the next @ref loadTilesContainer step to the next frame via a zero-delay timer.
             * @param p_remainingTileToPool Remaining pool count to pass to the next step.
             * @param p_remainingTilePositionToSpawn Positions left to spawn.
             * @param p_packSize Batch size for the next step.
            **/
            void scheduleLoadTilesNextFrame(const int p_remainingTileToPool, const Array &p_remainingTilePositionToSpawn, const int p_packSize);

            /**
             * @brief Timer callback that continues asynchronous @ref loadTilesContainer.
            **/
            void onLoadTilesPendingTimeout();


            /**
             * @brief Snaps a point on the map plane to the nearest axial hex coordinates.
             * @param p_focusPoint Position on the hex layout plane (same space as camera center projection).
             * @return Axial (q, r) integer coordinates.
            **/
            const Vector2i getMapPositionFromPlanPosition(const Vector2 p_focusPoint);

            /**
             * @brief Lists every axial tile position inside a hex disk (ring enumeration).
             * @param p_centerTilePosition Disk center in axial coordinates.
             * @param p_radius Inclusive hex distance from the center to the edge of the disk.
             * @return Array of @c Vector2i positions covering the disk.
            **/
            const Array getDiskTilesPosition(const Vector2i p_centerTilePosition, const int p_radius);

            /**
             * @brief Hex grid distance between two axial positions (cube metric).
             * @param p_tilePositionA First tile (q, r).
             * @param p_tilePositionB Second tile (q, r).
             * @return Number of steps along the hex grid between the two cells.
            **/
            const int getHexTilesDistance(const Vector2i p_tilePositionA, const Vector2i p_tilePositionB);
    };
}
