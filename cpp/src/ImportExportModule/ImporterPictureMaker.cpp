#include "ImporterPictureMaker.h"

// Godot include
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace ImportExportModule {

    const float CAPTURE_DEFAULT_MIN_RADIUS = 0.5f;
    const float CAPTURE_MODEL_TARGET_RADIUS = 0.45f;
    const float CAPTURE_CAMERA_DISTANCE_RATIO = 3.0f;
    const float CAPTURE_CAMERA_DISTANCE_MIN = 0.2f;

    void ImporterPictureMaker::_bind_methods() {
    }

    void ImporterPictureMaker::mergeMeshAABBs(Node* p_node, AABB& r_out, bool& r_had) {
        if (p_node == nullptr) {
            return;
        }
        const GeometryInstance3D* g = Object::cast_to<GeometryInstance3D>(p_node);
        if (g != nullptr) {
            const AABB la = g->get_aabb();
            const Transform3D t = g->get_global_transform();
            for (int i = 0; i < 8; i++) {
                const Vector3 world_p = t.xform(la.get_endpoint(i));
                if (!r_had) {
                    r_out = AABB(world_p, Vector3(0, 0, 0));
                    r_had = true;
                } else {
                    r_out.expand_to(world_p);
                }
            }
        }
        const TypedArray<Node> ch = p_node->get_children();
        for (int i = 0; i < ch.size(); i++) {
            Node* c = Object::cast_to<Node>(ch[i]);
            if (c != nullptr) {
                mergeMeshAABBs(c, r_out, r_had);
            }
        }
    }

    ImporterPictureMaker::ImporterPictureMaker() :
        m_state{E_ImporterPictureMakerState::Waiting},
        m_camera{nullptr},
        m_object{nullptr},
        m_object3D{nullptr} {
    }

    ImporterPictureMaker::~ImporterPictureMaker() {
    }

    void ImporterPictureMaker::_ready() {
        m_camera = Object::cast_to<Camera3D>(get_node_or_null(NodePath("PreviewCamera")));
        if (m_camera == nullptr) {
            UtilityFunctions::push_error("ImporterPictureMaker: missing PreviewCamera child");
            return;
        }
        m_defaultCameraLocal = m_camera->get_transform();
    }

    void ImporterPictureMaker::makeAPicture(const String& p_objectPath) {
        if (m_state != E_ImporterPictureMakerState::Waiting) {
            UtilityFunctions::push_error("ImporterPictureMaker: makeAPicture only allowed in Waiting");
            return;
        }
        if (p_objectPath.is_empty()) {
            UtilityFunctions::push_error("ImporterPictureMaker: empty glTF path");
            return;
        }
        String full = p_objectPath.strip_edges();
        if (!FileAccess::file_exists(full)) {
            UtilityFunctions::push_error("ImporterPictureMaker: file not found: ", full);
            return;
        }
        const String low = full.to_lower();
        if (!low.ends_with(".gltf") && !low.ends_with(".glb")) {
            UtilityFunctions::push_error("ImporterPictureMaker: not a .gltf or .glb: ", full);
            return;
        }
        m_objectPath = full;
    }

    ImporterPictureMaker::E_ImporterPictureMakerState ImporterPictureMaker::stepProgress() {
        switch (m_state) {
            case E_ImporterPictureMakerState::Waiting: {
                if (m_objectPath.is_empty()) {
                    return E_ImporterPictureMakerState::Waiting;
                }
                create3DObject();
                if (m_state == E_ImporterPictureMakerState::Waiting) {
                    return E_ImporterPictureMakerState::Waiting;
                }
                return E_ImporterPictureMakerState::Created;
            }
            case E_ImporterPictureMakerState::Created: {
                calibrateCamera();
                m_state = E_ImporterPictureMakerState::Calibrated;
                return E_ImporterPictureMakerState::Calibrated;
            }
            case E_ImporterPictureMakerState::Calibrated: {
                screenShotObject();
                m_state = E_ImporterPictureMakerState::Pictured;
                return E_ImporterPictureMakerState::Pictured;
            }
            case E_ImporterPictureMakerState::Pictured: {
                destructObject();
                return E_ImporterPictureMakerState::Waiting;
            }
        }
        return E_ImporterPictureMakerState::Waiting;
    }

    void ImporterPictureMaker::create3DObject() {
        if (m_objectPath.begins_with("user://")) {
            Ref<GLTFDocument> gltf_doc;
            gltf_doc.instantiate();
            Ref<GLTFState> gltf_state;
            gltf_state.instantiate();
            const String base_dir = m_objectPath.get_base_dir();
            const Error gltf_err
                = gltf_doc->append_from_file(m_objectPath, gltf_state, 0, base_dir);
            if (gltf_err != OK) {
                UtilityFunctions::push_error(
                    String("ImporterPictureMaker: GLTFDocument.append_from_file failed (")
                        + String::num_int64(static_cast<int64_t>(gltf_err)) + String("): ") + m_objectPath);
                m_objectPath = String();
                m_state = E_ImporterPictureMakerState::Waiting;
                return;
            }
            m_object = gltf_doc->generate_scene(gltf_state);
        } else {
            const Ref<PackedScene> sc
                = ResourceLoader::get_singleton()->load(m_objectPath, "PackedScene");
            if (sc.is_null()) {
                UtilityFunctions::push_error(
                    String("ImporterPictureMaker: failed to load PackedScene: ") + m_objectPath);
                m_objectPath = String();
                m_state = E_ImporterPictureMakerState::Waiting;
                return;
            }
            m_object = sc->instantiate();
        }
        if (m_object == nullptr) {
            UtilityFunctions::push_error("ImporterPictureMaker: glTF root / instantiate is null");
            m_objectPath = String();
            m_state = E_ImporterPictureMakerState::Waiting;
            return;
        }
        m_object3D = Object::cast_to<Node3D>(m_object);
        if (m_object3D == nullptr) {
            UtilityFunctions::push_error("ImporterPictureMaker: instantiated root is not Node3D");
            m_object->queue_free();
            m_object = nullptr;
            m_objectPath = String();
            m_state = E_ImporterPictureMakerState::Waiting;
            return;
        }
        add_child(m_object);
        m_state = E_ImporterPictureMakerState::Created;
    }

    void ImporterPictureMaker::calibrateCamera() {
        if (m_camera == nullptr) {
            return;
        }
        if (m_object == nullptr || m_object3D == nullptr) {
            return;
        }

        AABB aabb;
        bool had = false;
        mergeMeshAABBs(m_object, aabb, had);
        if (!had || aabb.get_size().length_squared() < 1e-10f) {
            aabb = AABB(Vector3(-CAPTURE_DEFAULT_MIN_RADIUS, -CAPTURE_DEFAULT_MIN_RADIUS, -CAPTURE_DEFAULT_MIN_RADIUS),
                Vector3(CAPTURE_DEFAULT_MIN_RADIUS * 2.0f, CAPTURE_DEFAULT_MIN_RADIUS * 2.0f, CAPTURE_DEFAULT_MIN_RADIUS * 2.0f));
        }

        m_object3D->set_global_position(m_object3D->get_global_position() - aabb.get_center());
        mergeMeshAABBs(m_object, aabb, had);
        float model_radius = 0.5f * aabb.get_size().length();
        if (model_radius < CAPTURE_DEFAULT_MIN_RADIUS) {
            model_radius = CAPTURE_DEFAULT_MIN_RADIUS;
        }
        m_object3D->set_scale(m_object3D->get_scale() * (CAPTURE_MODEL_TARGET_RADIUS / model_radius));

        had = false;
        mergeMeshAABBs(m_object, aabb, had);
        model_radius = 0.5f * aabb.get_size().length();
        if (model_radius < CAPTURE_DEFAULT_MIN_RADIUS) {
            model_radius = CAPTURE_DEFAULT_MIN_RADIUS;
        }
        const Transform3D camera_transform = m_camera->get_global_transform();
        const Vector3 camera_forward = camera_transform.basis.xform(Vector3(0.0f, 0.0f, -1.0f)).normalized();
        float camera_distance = model_radius * CAPTURE_CAMERA_DISTANCE_RATIO;
        if (camera_distance < CAPTURE_CAMERA_DISTANCE_MIN) {
            camera_distance = CAPTURE_CAMERA_DISTANCE_MIN;
        }
        m_camera->set_global_position(aabb.get_center() - camera_forward * camera_distance);
    }

    void ImporterPictureMaker::screenShotObject() {
        if (m_camera == nullptr) {
            m_objectPath = String();
            return;
        }
        Ref<ViewportTexture> tex = get_texture();
        if (tex.is_null()) {
            UtilityFunctions::push_error("ImporterPictureMaker: no viewport texture");
            m_objectPath = String();
            return;
        }
        Ref<Image> img = tex->get_image();
        if (img.is_null()) {
            UtilityFunctions::push_error("ImporterPictureMaker: get_image() failed (try step on next frame after calibrate)");
            m_objectPath = String();
            return;
        }
        const String base_dir = m_objectPath.get_base_dir();
        const String file_base = m_objectPath.get_file().get_basename();
        Ref<DirAccess> d = DirAccess::open(base_dir);
        if (d.is_valid()) {
            if (!d->dir_exists("capture")) {
                const Error m = d->make_dir("capture");
                if (m != OK) {
                    UtilityFunctions::push_error("ImporterPictureMaker: make_dir capture failed: ", m);
                }
            }
        } else {
            UtilityFunctions::push_error("ImporterPictureMaker: DirAccess::open base_dir: ", base_dir);
        }
        const String out = base_dir.path_join("capture").path_join(file_base + String(".png"));
        const Error e = img->save_png(out);
        if (e != OK) {
            UtilityFunctions::push_error("ImporterPictureMaker: save_png failed: ", (int64_t)e, " at ", out);
        }
    }

    void ImporterPictureMaker::destructObject() {
        if (m_object != nullptr) {
            m_object->queue_free();
            m_object = nullptr;
        }
        m_object3D = nullptr;
        m_objectPath = String();
        if (m_camera != nullptr) {
            m_camera->set_transform(m_defaultCameraLocal);
        }
        m_state = E_ImporterPictureMakerState::Waiting;
    }

}
