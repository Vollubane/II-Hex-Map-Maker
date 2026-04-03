#include "CameraMap.h"

// Custom include
#include "RootModule/RootMap.h"

// Godot include
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>

using namespace godot;
namespace CameraModule {

    const Vector2 ANGULAR_LIMIT = Vector2(Math::deg_to_rad(-30.0), Math::deg_to_rad(-89.9));
    const Vector2 HEIGHT_LIMIT = Vector2(5.0, 40.0);
    const Vector2 ZOOM_STEP = Vector2(2.0, Math::deg_to_rad(4.0));
    const Vector2 CAMERA_MOVE_STEP = Vector2(Math::deg_to_rad(0.25), 0.05);
    const Vector3 WORLD_UP = Vector3(0.0, 1.0, 0.0);

    void CameraMap::_bind_methods() {
        ClassDB::bind_method(D_METHOD("handleSceneReady"), &CameraMap::handleSceneReady);
        ClassDB::bind_method(D_METHOD("handleCameraMapModeChanged", "camera_mode"), &CameraMap::handleCameraMapModeChanged);
    }

    CameraMap::CameraMap() :
        m_hexMapEventBus{nullptr},
        m_isSceneReady{false},
        m_viewCenterMapPosition{0,0},
        m_cameraMode{E_CameraMapMovementType::CAMERA_MAP_CLASSIC_MODE},
        m_orbitYaw{0.0},
        m_orbitPitch{ANGULAR_LIMIT.y},
        m_orbitDistance{HEIGHT_LIMIT.x + (HEIGHT_LIMIT.y - HEIGHT_LIMIT.x) / 2.0}
    {
    }

    CameraMap::~CameraMap() {
    }

    void CameraMap::_ready() {
        m_hexMapEventBus = get_node<Node>("/root/HexMapEventBus");
        if (!m_hexMapEventBus) {
            UtilityFunctions::push_error("CameraMap ready : Can't find HexMapEventBus");
            return;
        }
        m_hexMapEventBus->connect("scene_ready", Callable(this, "handleSceneReady"));

        clampOrbitPitchToDistanceLimits();
        updateTransformFromViewCenter();

        m_hexMapEventBus->emit_signal("camera_ready");
        if (!Object::cast_to<RootModule::RootMap>(this->get_parent())) {
            m_isSceneReady = true; // return true in debug
        }
    }

    void CameraMap::_input(const Ref<InputEvent> &p_event) {
        if(!m_isSceneReady) return;

        if(Input::get_singleton()->is_action_just_pressed("CameraZoom+")) {
            handleZoomIn();
            return;
        }
        if (Input::get_singleton()->is_action_just_pressed("CameraZoom-")) {
            handleZoomOut();
            return;
        }

        Ref<InputEventMouseMotion> mouse_motion = p_event;
        if (!mouse_motion.is_valid()) return; // Camera mouvement only run with mouse mouvement
        Vector2 mouseDelta = mouse_motion->get_relative();
        switch (m_cameraMode) {
            case E_CameraMapMovementType::CAMERA_MAP_CLASSIC_MODE: {
                if(Input::get_singleton()->is_action_pressed("CtrlCameraMouvement")) {
                    handleTopDownMovement(mouseDelta);
                } else if(Input::get_singleton()->is_action_pressed("CameraMouvement")) {
                    handleOrbitMovement(mouseDelta);
                }
                break;
            }
            case E_CameraMapMovementType::CAMERA_MAP_TOPDOWN_MODE: {
                if(Input::get_singleton()->is_action_pressed("CameraMouvement")) {
                    handleTopDownMovement(mouseDelta);
                }
                break;
            }
            case E_CameraMapMovementType::CAMERA_MAP_MOUSE_MODE: {
                if(Input::get_singleton()->is_action_pressed("Interaction")) {
                    handleTopDownMovement(mouseDelta);
                }
                break;
            }
            default:
                break;
        }
    }

    void CameraMap::handleSceneReady() {
        m_isSceneReady = true;
    }

    void CameraMap::handleCameraMapModeChanged(const int p_cameraMode) {
        m_cameraMode = static_cast<E_CameraMapMovementType>(p_cameraMode);
    }

    void CameraMap::updateViewCenterMapPosition(const Vector2 p_viewCenterMapPosition) {
        if(p_viewCenterMapPosition == m_viewCenterMapPosition) return;

        m_viewCenterMapPosition = p_viewCenterMapPosition;
        m_hexMapEventBus->emit_signal("center_map_position", m_viewCenterMapPosition);
    }

    Vector2 CameraMap::computeViewCenterMapPosition() {
        const Vector2 viewportCenter = get_viewport()->get_visible_rect().size * 0.5;
        const Vector3 rayOrigin = project_ray_origin(viewportCenter);
        const Vector3 rayDirection = project_ray_normal(viewportCenter);

        if (Math::is_zero_approx(rayDirection.y)) {
            UtilityFunctions::push_warning("CameraMap computeViewCenterMapPosition : Near 0° angular of the camera");
            return Vector2();
        }
        const double t = -rayOrigin.y / rayDirection.y;

        const Vector3 hitPoint = rayOrigin + rayDirection * t;
        return Vector2(hitPoint.x, hitPoint.z);
    }

    double CameraMap::getMaxPitchForDistance() const {
        const double minDistance = static_cast<double>(HEIGHT_LIMIT.x);
        const double maxDistance = static_cast<double>(HEIGHT_LIMIT.y);
        const double freeDistanceStart = minDistance + (maxDistance - minDistance) * 0.5;

        if (m_orbitDistance >= freeDistanceStart) {
            return static_cast<double>(ANGULAR_LIMIT.x);
        }

        if (Math::is_zero_approx(freeDistanceStart - minDistance)) {
            return static_cast<double>(ANGULAR_LIMIT.y);
        }

        const double normalizedDistance = (m_orbitDistance - minDistance) / (freeDistanceStart - minDistance);
        const double clampedDistance = Math::clamp(normalizedDistance, 0.0, 1.0);

        return static_cast<double>(ANGULAR_LIMIT.y) +
               (static_cast<double>(ANGULAR_LIMIT.x) - static_cast<double>(ANGULAR_LIMIT.y)) * clampedDistance;
    }

    void CameraMap::clampOrbitPitchToDistanceLimits() {
        m_orbitPitch = Math::clamp(
            m_orbitPitch,
            static_cast<double>(ANGULAR_LIMIT.y),
            getMaxPitchForDistance()
        );
    }

    void CameraMap::updateTransformFromViewCenter() {
        const Vector3 focusPosition = Vector3(m_viewCenterMapPosition.x, 0.0, m_viewCenterMapPosition.y);
        const double pitchMagnitude = -m_orbitPitch;
        const double horizontalRadius = m_orbitDistance * Math::cos(pitchMagnitude);
        const double heightOffset = m_orbitDistance * Math::sin(pitchMagnitude);

        Vector3 cameraPosition = Vector3(
            focusPosition.x + horizontalRadius * Math::sin(m_orbitYaw),
            focusPosition.y + heightOffset,
            focusPosition.z + horizontalRadius * Math::cos(m_orbitYaw)
        );

        set_position(cameraPosition);
        look_at(focusPosition, WORLD_UP);
    }

    void CameraMap::handleZoomIn() {
        m_orbitDistance = Math::max(
            m_orbitDistance - static_cast<double>(ZOOM_STEP.x),
            static_cast<double>(HEIGHT_LIMIT.x)
        );
        clampOrbitPitchToDistanceLimits();

        updateTransformFromViewCenter();
    }

    void CameraMap::handleZoomOut() {
        m_orbitDistance = Math::min(
            m_orbitDistance + static_cast<double>(ZOOM_STEP.x),
            static_cast<double>(HEIGHT_LIMIT.y)
        );
        clampOrbitPitchToDistanceLimits();

        updateTransformFromViewCenter();
    }

    void CameraMap::handleOrbitMovement(const Vector2 p_mouseDelta) {
        m_orbitYaw -= p_mouseDelta.x * CAMERA_MOVE_STEP.x;
        m_orbitPitch -= p_mouseDelta.y * CAMERA_MOVE_STEP.x;
        clampOrbitPitchToDistanceLimits();

        updateTransformFromViewCenter();
    }

    void CameraMap::handleTopDownMovement(const Vector2 p_mouseDelta) {
        const real_t yaw = static_cast<real_t>(m_orbitYaw);

        Vector3 right = Vector3(Math::cos(yaw), 0.0, -Math::sin(yaw));
        Vector3 forward = Vector3(-Math::sin(yaw), 0.0, -Math::cos(yaw));
        Vector3 translation = (-p_mouseDelta.x * right + p_mouseDelta.y * forward) * CAMERA_MOVE_STEP.y;

        updateViewCenterMapPosition(m_viewCenterMapPosition + Vector2(translation.x, translation.z));
        updateTransformFromViewCenter();
    }
}
