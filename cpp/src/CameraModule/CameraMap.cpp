#include "CameraMap.h"

// Custom include
#include "RootModule/RootMap.h"

// Godot include
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>

using namespace godot;
namespace CameraModule {

    const Vector2 ANGULAR_LIMIT = Vector2(-30, -90);
    const Vector2 HEIGHT_LIMIT  = Vector2(10, 40);

    void CameraMap::_bind_methods() {
        ClassDB::bind_method(D_METHOD("handleSceneReady"), &CameraMap::handleSceneReady);
    }

    CameraMap::CameraMap() :
        m_isSceneReady{false},
        m_viewCenterMapPosition{0,0},
        m_cameraMode{CameraMapMovementType::CAMERA_MAP_CLASSIC_MODE}
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

        this->set_position(Vector3(0,HEIGHT_LIMIT[0]+(HEIGHT_LIMIT[1]-HEIGHT_LIMIT[0])/2,0));
        this->set_rotation(Vector3(ANGULAR_LIMIT[1],0,0));

        if(!Object::cast_to<RootModule::RootMap>(this->get_parent())) m_isSceneReady=true; // return true in debug
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
            case CameraMapMovementType::CAMERA_MAP_CLASSIC_MODE: {
                if(Input::get_singleton()->is_action_pressed("CtrlCameraMouvement")) {
                    handleTopDownMovement(mouseDelta);
                } else if(Input::get_singleton()->is_action_pressed("CameraMouvement")) {
                    handleOrbitMovement(mouseDelta);
                }
                break;
            }
            case CameraMapMovementType::CAMERA_MAP_TOPDOWN_MODE: {
                if(Input::get_singleton()->is_action_pressed("CameraMouvement")) {
                    handleTopDownMovement(mouseDelta);
                }
            }
            case CameraMapMovementType::CAMERA_MAP_MOUSE_MODE: {
                if(Input::get_singleton()->is_action_pressed("Interaction")) {
                    handleTopDownMovement(mouseDelta);
                }
            }
            default:
                break;
        }
    }

    void CameraMap::handleSceneReady() {
        m_isSceneReady = true;
    }

    void CameraMap::handleCameraMapModeChanged(const int p_cameraMode) {
        m_cameraMode = static_cast<CameraMapMovementType>(p_cameraMode);
    }

    void CameraMap::updateViewCenterMapPosition(const Vector2 p_viewCenterMapPosition) {
        if((p_viewCenterMapPosition == m_viewCenterMapPosition) || p_viewCenterMapPosition == Vector2(0,0)) return;

        m_viewCenterMapPosition = p_viewCenterMapPosition;
        m_hexMapEventBus->emit_signal("center_map_position", m_viewCenterMapPosition);
    }

    const Vector2 CameraMap::computeViewCenterMapPosition() {
        Vector2 viewportCenter = get_viewport()->get_visible_rect().size * 0.5;
        Vector3 rayOrigin = project_ray_origin(viewportCenter);
        Vector3 rayDirection = project_ray_normal(viewportCenter);

        if (Math::is_zero_approx(rayDirection.y)) {
            UtilityFunctions::push_warning("CameraMap computeViewCenterMapPosition : Near 0° angular of the camera");
            return Vector2();
        }
        double t = -rayOrigin.y / rayDirection.y;

        Vector3 hitPoint = rayOrigin + rayDirection * t;
        return Vector2(hitPoint.x, hitPoint.z);
    }
}
