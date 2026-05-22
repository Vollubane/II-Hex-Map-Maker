#include "Importer.h"
#include "Importer_Constants.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace ImportExportModule {

    void Importer::_bind_methods() {
        ClassDB::bind_method(D_METHOD("setupImportNewAssets", "asset_pack_path", "import_assets_path"), &Importer::setupImportNewAssets);
        ClassDB::bind_method(D_METHOD("setupRemoveAssetsFromPack", "asset_pack_path", "pack_gltf_file_names"), &Importer::setupRemoveAssetsFromPack);
        ClassDB::bind_method(D_METHOD("setupRepareAssetsPack", "asset_pack_path"), &Importer::setupRepareAssetsPack);
    }

    Importer::Importer() :
        m_importerState{E_ImporterState::Waiting},
        m_timeBudget(0.0)
    {}

    Importer::~Importer() {}

    void Importer::_ready() {
        m_importerPictureMakerScene = ResourceLoader::get_singleton()->load(String(M_ModulesList.at("IMPORTER_PICTURE_MAKER").c_str()));
        if(m_importerPictureMakerScene.is_null()) {
            UtilityFunctions::push_error("Importer: ready, invalid ImporterPictureMaker scene resource");
            return;
        }
        Node* testNode = m_importerPictureMakerScene->instantiate();
        if(!testNode) {
            UtilityFunctions::push_error("Importer: ready, invalid ImporterPictureMaker scene");
            return;
        }
        testNode->queue_free();
    }

    void Importer::_process(double p_delta) {
        (void)p_delta;
        m_timeBudget = 1e6 * (1.0 / double(FPS_IN_CALCULATION)) * double(FPS_RATIO_BUDGET);
        switch(m_importerState) {
            case E_ImporterState::Waiting:
                break;
            case E_ImporterState::Destruct:
                queue_free();
                break;
            case E_ImporterState::Copying:
                progressImportCopyStateForFrame();
                break;
            case E_ImporterState::Picturing: {
                if(!m_picturing.toCapture.is_empty() || !m_picturing.active.is_empty())
                    runPicturingPhase();
                if(m_picturing.toCapture.is_empty() && m_picturing.active.is_empty()) {
                    if(m_picturing.followsRepair) {
                        m_importerState = E_ImporterState::RepairingPack;
                        m_repair.subPhase = m_picturing.returnPhase;
                        m_picturing.followsRepair = false;
                    } else {
                        queue_free();
                    }
                }
            } break;
            case E_ImporterState::RemovingAssets:
                runRemovingAssetsSlice();
                if(m_remove.subPhase == E_RemoveSubPhase::SubDone) queue_free();
                break;
            case E_ImporterState::RepairingPack:
                runRepairPackSlice();
                if(m_repair.subPhase == E_RepairSubPhase::SubDone) queue_free();
                break;
            default:
                UtilityFunctions::push_error(String("Importer: _process, unexpected FSM state: ")
                    + String::num_int64(int64_t(m_importerState)));
                break;
        }
    }

    bool Importer::setupImportNewAssets(const String& p_assetPackPath, const String& p_importAssetsPath) {
        if(m_importerState != E_ImporterState::Waiting) {
            UtilityFunctions::push_error("Importer: setup import, busy");
            return false;
        }
        m_importSourceRoot = p_importAssetsPath.simplify_path();
        if(!loadPackManifestFromDiskOrDestruct(p_assetPackPath, "setup")) {
            queue_free();
            return false;
        }
        Array found;
        if(!enumerateGltfUnderImportPath(p_importAssetsPath, found, 0))
            return failSetup("Importer: setup, import path invalid or max depth exceeded");
        if(found.is_empty())
            return failSetup("Importer: setup, no .gltf under import path");
        if(!buildImportPlan(found))
            return failSetup("Importer: setup, plan failed (IO or disk space)");
        m_importerState = E_ImporterState::Copying;
        return true;
    }

    bool Importer::setupRepareAssetsPack(const String& p_assetsPackPath) {
        if(m_importerState != E_ImporterState::Waiting) {
            UtilityFunctions::push_error("Importer: repair, busy");
            return false;
        }
        if(p_assetsPackPath.is_empty())
            return failSetup("Importer: repair, empty asset pack path");
        if(!loadPackManifestFromDiskOrDestruct(p_assetsPackPath, "repair")) {
            queue_free();
            return false;
        }
        m_picturing.returnPhase  = E_RepairSubPhase::FinalizeRepairWrite;
        m_repair.subPhase        = E_RepairSubPhase::BuildOrphanGltfList;
        m_importerState          = E_ImporterState::RepairingPack;
        return true;
    }

    bool Importer::setupRemoveAssetsFromPack(const String& p_assetPackPath, const Array& p_packGltfFileNames) {
        if(m_importerState != E_ImporterState::Waiting) {
            UtilityFunctions::push_error("Importer: remove assets, busy");
            return false;
        }
        if(p_assetPackPath.is_empty())
            return failSetup("Importer: remove assets, empty asset pack path");
        if(p_packGltfFileNames.is_empty())
            return failSetup("Importer: remove assets, empty name list");
        if(!loadPackManifestFromDiskOrDestruct(p_assetPackPath, "remove assets")) {
            queue_free();
            return false;
        }
        const Dictionary assets = manifestAssetsDict();
        Dictionary queued;
        for(int64_t i = 0; i < p_packGltfFileNames.size(); ++i) {
            const Variant& v = p_packGltfFileNames[i];
            if(v.get_type() != Variant::STRING && v.get_type() != Variant::STRING_NAME) {
                UtilityFunctions::push_warning(String("Importer: remove assets, ignore non-string at index ")
                    + String::num_int64(i));
                continue;
            }
            const String key = String(v);
            if(key.is_empty()) {
                UtilityFunctions::push_warning(String("Importer: remove assets, ignore empty name at index ")
                    + String::num_int64(i));
                continue;
            }
            if(!assets.has(key)) {
                UtilityFunctions::push_warning(String("Importer: remove assets, ignore absent from manifest: ") + key);
                continue;
            }
            if(queued.has(Variant(key))) continue;
            queued[Variant(key)] = true;
            m_remove.gltfKeys.append(key);
        }
        if(m_remove.gltfKeys.is_empty())
            return failSetup(String("Importer: remove assets, no valid .gltf key to remove (")
                + String::num_int64(p_packGltfFileNames.size()) + String(" candidate(s))"));
        m_remove.subPhase = E_RemoveSubPhase::ListedAssetsAndDrainDeletions;
        m_importerState   = E_ImporterState::RemovingAssets;
        return true;
    }

    const bool Importer::failSetup(const String& p_msg) {
        UtilityFunctions::push_error(p_msg);
        m_importerState = E_ImporterState::Destruct;
        queue_free();
        return false;
    }

}
