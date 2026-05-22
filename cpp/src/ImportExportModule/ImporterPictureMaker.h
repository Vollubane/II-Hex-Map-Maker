#pragma once

// Godot include
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

using namespace godot;

namespace ImportExportModule {

    /**
     * @brief SubViewport used for offline thumbnails: loads one glTF, frames the camera, saves PNG under capture/
     *        next to the file, then tears down the model.
     */
    class ImporterPictureMaker : public SubViewport {
        GDCLASS(ImporterPictureMaker, SubViewport)

        public:
            enum class E_ImporterPictureMakerState {
                Waiting,
                Created,
                Calibrated,
                WaitAfterCalibrate,
                WaitAfterCalibrate2,
                Pictured
            };

        private:
            E_ImporterPictureMakerState m_state; //!< Current step in the capture cycle, see E_ImporterPictureMakerState.
            Camera3D* m_camera;                  //!< Child PreviewCamera; resolved in _ready.
            String m_objectPath;                 //!< Resolved glTF path to capture; cleared when idle.
            Node* m_object;                      //!< Instantiated or generated scene root while capturing.
            Node3D* m_object3D;                  //!< Cast of m_object when root is Node3D (required for framing).
            Transform3D m_defaultCameraLocal; //!< Camera transform restored in destructObject.

        protected:
            /**
             * @brief Binds methods exposed to Godot.
            **/
            static void _bind_methods();

        public:
            /**
             * @brief Constructor of the node.
            **/
            ImporterPictureMaker();

            /**
             * @brief Destructor.
            **/
            ~ImporterPictureMaker();

            /**
             * @brief Called when the node enters the scene tree: resolves PreviewCamera and stores its local transform.
            **/
            void _ready();

            /**
             * @brief Advances at most one FSM transition (load, calibrate, screenshot, or teardown).
             * @return State after this call; may stay Waiting when no path is queued.
            **/
            E_ImporterPictureMakerState stepProgress();

            /**
             * @brief Queues capture for one absolute glTF path (checks file and extension; only when state is Waiting).
             * @param p_objectPath Resolved .gltf or .glb path, typically user:// after pack copy.
             */
            void makeAPicture(const String& p_objectPath);

        private:
            /**
             * @brief Loads the model: GLTFDocument + generate_scene under user://, otherwise ResourceLoader as PackedScene.
            **/
            void create3DObject();

            /**
             * @brief Centers mesh at origin, scales toward CAPTURE_MODEL_TARGET_RADIUS, repositions camera.
            **/
            void calibrateCamera();

            /**
             * @brief Reads viewport texture, ensures capture/, writes capture/<basename>.png.
            **/
            void screenShotObject();

            /**
             * @brief Frees m_object, resets camera and Waiting state.
            **/
            void destructObject();

            /**
             * @brief Recursively merges world-space AABBs from GeometryInstance3D under p_root.
             * @param p_root Root to traverse.
             * @param r_out Aggregated AABB.
             * @param r_had Set true once any mesh contributed.
            **/
            void mergeMeshAABBs(Node* p_root, AABB& r_out, bool& r_had);
    };
}
