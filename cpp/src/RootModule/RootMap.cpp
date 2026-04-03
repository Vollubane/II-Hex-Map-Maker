#include "RootMap.h"

// Godot include
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace RootModule {



    void RootMap::_bind_methods() {
        ClassDB::bind_method(D_METHOD("onTilesContainerReady"), &RootMap::onTilesContainerReady);
        ClassDB::bind_method(D_METHOD("onCameraMapReady"), &RootMap::onCameraMapReady);
    }

    RootMap::RootMap() :
        m_hexMapEventBus{nullptr},
        m_remainingModulesInThread{},
        m_modulesReady{},
        m_isSceneInThread{false}
    {
    }

    RootMap::~RootMap() {
    }

    void RootMap::_process(double p_delta) {
        if ((!m_isSceneInThread) || (m_remainingModulesInThread.size() <= 0)) {
            return;
        }

        for (int indexModules = m_remainingModulesInThread.size() - 1; indexModules >= 0; indexModules--)
        {
            const String moduleName = m_remainingModulesInThread[indexModules];
            const String modulePath = getModulePath(moduleName);
            ResourceLoader::ThreadLoadStatus status = ResourceLoader::get_singleton()->load_threaded_get_status(modulePath);
            switch (status) {
                case ResourceLoader::THREAD_LOAD_INVALID_RESOURCE:
                    UtilityFunctions::push_error("RootMap process : ", moduleName, " module resource invalid.");
                    m_modulesReady[moduleName] = true;
                    m_remainingModulesInThread.remove_at(indexModules);
                    break;
                case ResourceLoader::THREAD_LOAD_IN_PROGRESS:
                    break;
                case ResourceLoader::THREAD_LOAD_FAILED:
                    UtilityFunctions::push_error("RootMap process : ", moduleName, " module don't load.");
                    m_modulesReady[moduleName] = true;
                    m_remainingModulesInThread.remove_at(indexModules);
                    break;
                case ResourceLoader::THREAD_LOAD_LOADED: {
                    Ref<Resource> resource = ResourceLoader::get_singleton()->load_threaded_get(modulePath);
                    if (resource.is_null()) {
                        UtilityFunctions::push_error("Loaded status but resource is null: ", modulePath);
                        m_modulesReady[moduleName] = true;
                        m_remainingModulesInThread.remove_at(indexModules);
                        break;
                    }
                    Ref<PackedScene> scene = resource;
                    if (scene.is_null()) {
                        UtilityFunctions::push_error("Resource is not a PackedScene: ", modulePath);
                        m_modulesReady[moduleName] = true;
                        m_remainingModulesInThread.remove_at(indexModules);
                        break;
                    }
                    initModule(scene, moduleName);
                    m_remainingModulesInThread.remove_at(indexModules);
                    break;
                }
                default:
                    UtilityFunctions::push_warning("Unknown threaded load status for: ", modulePath);
                    break;
            }
        }

        if (m_remainingModulesInThread.size() == 0) {
            m_isSceneInThread = false;
        }
    }

    void RootMap::_ready() {
        m_hexMapEventBus = get_node<Node>("/root/HexMapEventBus");
        if (!m_hexMapEventBus) {
            UtilityFunctions::push_error("RootMap ready : Can't find HexMapEventBus");
            return;
        }
        m_hexMapEventBus->connect("camera_ready", Callable(this, "onCameraMapReady"));
        m_hexMapEventBus->connect("tilesContainer_ready", Callable(this, "onTilesContainerReady"));


        m_remainingModulesInThread.append_array(Array::make("CAMERA_MAP", "TILES_CONTAINER"));
        for (int indexModules = 0; indexModules < m_remainingModulesInThread.size(); indexModules++)
        {
            const String moduleName = m_remainingModulesInThread[indexModules];
            m_modulesReady[moduleName] = false;
        }

        loadModulesInThread();
    }

    void RootMap::loadModulesInThread() {
        for (int indexModules = 0; indexModules < m_remainingModulesInThread.size(); indexModules++)
        {
            ResourceLoader::get_singleton()->load_threaded_request(
                getModulePath(m_remainingModulesInThread[indexModules]));
        }
        m_isSceneInThread = true;
    }

    void RootMap::initModule(const Ref<PackedScene> p_module, const String p_moduleName) {
        Node *instance = Object::cast_to<Node>(p_module->instantiate());
        if (!instance) {
            UtilityFunctions::push_error("RootMap : initModule", p_module, " can't be instanciate");
            m_modulesReady[p_moduleName] = true;
            return;
        }
        add_child(instance);
    }

    void RootMap::emitSceneReady() {
        m_hexMapEventBus->emit_signal("scene_ready");
    }

    void RootMap::onTilesContainerReady() {
        m_modulesReady["TILES_CONTAINER"] = true;
        Array moduleNames = m_modulesReady.keys();
        UtilityFunctions::print(moduleNames);
        for (int indexModules = 0; indexModules < moduleNames.size(); indexModules++)
        {
            const String moduleName = moduleNames[indexModules];
            if (!bool(m_modulesReady[moduleName])) {
                return;
            }
        }
        emitSceneReady();
    }

    void RootMap::onCameraMapReady() {
        m_modulesReady["CAMERA_MAP"] = true;
        Array moduleNames = m_modulesReady.keys();
        UtilityFunctions::print(moduleNames);
        for (int indexModules = 0; indexModules < moduleNames.size(); indexModules++)
        {
            const String moduleName = moduleNames[indexModules];
            if (!bool(m_modulesReady[moduleName])) {
                return;
            }
        }
        emitSceneReady();
    }

    const String RootMap::getModulePath(const String &p_moduleName) {
        return String(M_ModulesList.at(p_moduleName.utf8().get_data()).c_str());
    }
}
