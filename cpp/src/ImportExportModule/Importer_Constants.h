#pragma once

/**
 * @brief Internal manifest key literals and FSM tuning constants shared by all Importer segments.
 * @details Include only from Importer_*.cpp segment files. Not part of the public Importer API.
 **/
namespace ImportExportModule {

    inline constexpr float FPS_RATIO_BUDGET  = 0.5f;  //!< Fraction of one frame budget allocated to importer work.
    inline constexpr int   PICTURE_MAKER_MAX = 8;     //!< Maximum concurrent ImporterPictureMaker worker instances.

    inline constexpr const char* ASSETS_KEY    = "assets_data";  //!< Top-level manifest key for the assets table.
    inline constexpr const char* BIN_DATA_KEY  = "bin_data";     //!< Top-level manifest key for the binary sidecar table.
    inline constexpr const char* TEX_DATA_KEY  = "texture_data"; //!< Top-level manifest key for the texture sidecar table.
    inline constexpr const char* GROUPS_KEY    = "groups";       //!< Top-level manifest key for the groups array.
    inline constexpr const char* TRUE_PATH_KEY = "true_path";    //!< Row key storing the import-relative source path.
    inline constexpr const char* WEIGHT_KEY    = "weight";       //!< Row key storing the file size in bytes.
    inline constexpr const char* GROUP_KEY     = "group";        //!< Asset row key for the group path string.
    inline constexpr const char* BIN_ROW_KEY           = "bin";          //!< Asset row key for the embedded bin sub-dictionary.
    inline constexpr const char* TEX_ROW_KEY           = "texture";      //!< Asset row key for the embedded texture sub-dictionary.
    inline constexpr const char* DEDUP_COMPARE_INDEX_KEY = "compareIndex"; //!< Deduplication bucket key storing the current compare iterator position.

}
