#pragma once

// Custom include
#include "constant.h"

// Godot include
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
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
            E_CameraMapMovementType m_cameraMode;   //!< Current camera movement mode.
            double m_orbitYaw;                      //!< Horizontal orbit angle around the current focus point.
            double m_orbitPitch;                    //!< Vertical orbit angle around the current focus point.
            double m_orbitDistance;                 //!< Distance from the camera to the current focus point.

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
             * @param p_cameraMode New camera mode as an int.
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
            Vector2 computeViewCenterMapPosition();

            /**
             * @brief Recomputes the camera transform around the current map focus.
             * @details Applies orbit distance, yaw and pitch around the focus point.
             **/
            void updateTransformFromViewCenter();

            /**
             * @brief Gets the maximum allowed pitch for the current orbit distance.
             * @details The allowed pitch becomes closer to -90 degrees near the minimum zoom distance.
             */
            double getMaxPitchForDistance() const;

            /**
             * @brief Clamps the orbit pitch using the current orbit distance.
             * @details Keeps the pitch inside the dynamic limits allowed by the zoom level.
             */
            void clampOrbitPitchToDistanceLimits();

            /**
             * @brief Handles zoom-in input.
             * @details Decreases the camera distance to the current focus point.
            **/
            void handleZoomIn();

            /**
             * @brief Handles zoom-out input.
             * @details Increases the camera distance to the current focus point.
            **/
            void handleZoomOut();

            /**
             * @brief Handles orbit camera movement around the current focus point.
             * @param p_mouseDelta Relative mouse motion for the current input event.
             * @details Updates the orbit yaw and pitch without changing the orbit distance.
            **/
            void handleOrbitMovement(const Vector2 p_mouseDelta);

            /**
             * @brief Handles top-down camera movement.
             * @param p_mouseDelta Relative mouse motion for the current input event.
             * @details Moves the focus point on the map plane using the current yaw.
            **/
            void handleTopDownMovement(const Vector2 p_mouseDelta);
    };
}
