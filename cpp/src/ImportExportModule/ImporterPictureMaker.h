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
     * @brief Off-screen 256x256 SubViewport: loads a .gltf, fits the camera, captures PNG, then cleans up.
     * @details After makeAPicture(pack, relative_path), call stepProgress() once per state transition.
     *          Waiting -> (create instance) -> Created -> (calibrate camera) -> Calibrated -> (save PNG) ->
     *          Pictured -> (destroy + reset camera) -> Waiting. PNG: base_dir of the .gltf + "/capture/" + basename
     *          + ".png".
     */
    class ImporterPictureMaker : public SubViewport {
        GDCLASS(ImporterPictureMaker, SubViewport)

    public:
        enum class E_ImporterPictureMakerState { Waiting, Created, Calibrated, Pictured };

    private:
        E_ImporterPictureMakerState m_state;
        Camera3D* m_camera;
        String m_objectPath;
        Node* m_object;
        Node3D* m_object3D;
        Transform3D m_defaultCameraLocal;

    protected:
        static void _bind_methods();

    public:
        ImporterPictureMaker();
        ~ImporterPictureMaker();

        void _ready();

        E_ImporterPictureMakerState stepProgress();
        /**
         * @brief Queues a capture for one glTF path.
         * @param p_objectPath glTF path relative to pack or already resolved by caller.
         */
        void makeAPicture(const String& p_objectPath);

    private:
        void create3DObject();
        void calibrateCamera();
        void screenShotObject();
        void destructObject();

        void mergeMeshAABBs(Node* p_root, AABB& r_out, bool& r_had);
    };
}
