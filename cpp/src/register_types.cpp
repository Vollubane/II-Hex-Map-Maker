#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "CameraModule/CameraMap.h"
#include "HexMapDisplayerModule/TilesContainer.h"
#include "RootModule/RootMap.h"
#include "RootModule/RootTilesPack.h"
#include "TileModule/Tile3D.h"
#include "ImportExportModule/Importer.h"
#include "ImportExportModule/ImporterPictureMaker.h"

using namespace godot;

void initialize_ii_hex_map_maker_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    ClassDB::register_runtime_class<CameraModule::CameraMap>();

    ClassDB::register_runtime_class<HMDisplayerModule::TilesContainer>();

    ClassDB::register_runtime_class<TileModule::Tile3D>();

    ClassDB::register_runtime_class<RootModule::RootMap>();
    ClassDB::register_runtime_class<RootModule::RootTilesPack>();

    ClassDB::register_runtime_class<ImportExportModule::Importer>();
    ClassDB::register_runtime_class<ImportExportModule::ImporterPictureMaker>();

}

void uninitialize_ii_hex_map_maker_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {

    GDExtensionBool GDE_EXPORT cpp_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_ii_hex_map_maker_module);
        init_obj.register_terminator(uninitialize_ii_hex_map_maker_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }

}
