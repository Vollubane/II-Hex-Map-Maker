#pragma once

// Custom include
#include "constant.h"

// Godot include
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/core/class_db.hpp>

/**
 * @brief Controls the map camera movement and rotation.
 *
 * This node emits the coordinates of the tile at the center of the current
 * view so other modules can decide what needs to be loaded or updated.
 *
 * The rotation is applied around the center of the current view.
 */
using namespace godot;
namespace CameraModule {
    class CameraMap : public Camera3D {
        GDCLASS(CameraMap, Camera3D)

        private :

            Node* m_hexMapEventBus;                 //!< Pointer to the scene event bus used to receive and emit HexMap signals.
            bool m_isSceneReady;                    //!< Whether the scene is fully initialized.
            Vector2 m_viewCenterMapPosition;        //!< Map coordinates at the center of the view.
            CameraMapMovementType m_cameraMode;     //!< Current camera movement mode.

        protected:
            /**
             * @brief Binds methods exposed to Godot.
            **/
            static void _bind_methods();

        public:

            /**
             * @brief Constructor of the node.
            **/
            CameraMap();

            /**
             * @brief Destructor.
            **/
            ~CameraMap();

            /**
             * @brief Called when the node enters the scene tree.
            **/
            void _ready();

            /**
             * @brief Handles input used to move the camera.
            **/
            void _input(const Ref<InputEvent> &p_event);

        private:

            /**
             * @brief Called when the scene is ready and camera movement can start.
            **/
            void handleSceneReady();

            /**
             * @brief Updates the current camera movement mode.
             * @param p_cameraMode New camera mode. An int is used because Godot
             * signals do not transmit this enum directly.
             */
            void handleCameraMapModeChanged(const int p_cameraMode);

            /**
             * @brief Updates and emits the current center position of the view.
             * @param p_viewCenterMapPosition New map position at the center of the view.
             */
            void updateViewCenterMapPosition(const Vector2 p_viewCenterMapPosition);

            /**
             * @brief Computes the center of the current view on the map plane.
             * @return A 2D position on the map.
            **/
            const Vector2 computeViewCenterMapPosition();

            /**
             * @brief Handles zoom-in input. Decreases the camera distance to the current focus point.
             */
            void handleZoomIn();

            /**
             * @brief Handles zoom-out input. Increases the camera distance to the current focus point.
             */
            void handleZoomOut();

            /**
             * @brief Handles orbit camera movement around the current focus point.
             * @param p_mouseDelta Relative mouse motion for the current input event.
             */
            void handleOrbitMovement(const Vector2 p_mouseDelta);

            /**
             * @brief Handles top-down camera movement.
             * @param p_mouseDelta Relative mouse motion for the current input event.
             */
            void handleTopDownMovement(const Vector2 p_mouseDelta);
    };
}
