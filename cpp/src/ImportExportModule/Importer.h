#pragma once

// Custom include
#include "constant.h"
#include "ImportExportModule/ImporterPictureMaker.h"

// Godot include
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>

using namespace godot;

namespace ImportExportModule {
    /**
     * @brief Godot node that imports glTF and sidecar files into a hex asset pack, updates the manifest, and
     *        deduplicates matching files after copy.
     *
     * When the import state is Copying, work is time-sliced in process using a per-frame microsecond budget.
     * JSON keys follow the string table M_MANIFEST_JSON_KEY_STRINGS in constant.h.
     */
    class Importer : public Node {
        GDCLASS(Importer, Node)

        private :

            /**
             * @brief Coarse FSM: idle, failed setup, time-sliced file copy, or handoff to post-import.
            **/
            enum class E_ImporterState { Waiting, Destruct, Copying, Picturing };

            /**
             * @brief Gltf copy queue, then sidecar copies, then manifest deduplication in one frame slice.
            **/
            enum class E_CopySubPhase { Gltf, Sidecar, Dedup, SubDone };

            Dictionary m_assetsPackManifest;                 //!< In-memory pack manifeste.json: assets_data, bin_data, texture_data, groups.
            E_ImporterState m_importerState;                 //!< FSM, see E_ImporterState.
            E_CopySubPhase m_copySubPhase;                   //!< Gltf / sidecar / dedup sub-phase, see E_CopySubPhase.
            String m_assetPackPath;                          //!< Target asset pack root directory.
            String m_importRootWithSlash;                    //!< Source root with a trailing slash for true_path resolution.

            Ref<PackedScene> m_importerPictureMakerScene;   //!< Preloaded ImporterPictureMaker scene (smoke-tested in _ready).
            /** glTFs under the pack to capture: filled during import, pruned in deduplicateGltfAssetsInManifest, then
             *  paths are remove_at(0) when given to a picture maker. */
            Array m_gltfAssetsToCapturePath;
            /** Pool nodes available for a new makeAPicture. */
            Array m_idlePictureMakers;
            /** Assigned a path, must be stepped (one step per frame). */
            Array m_activePictureMakers;

            Array m_plannedGltfItems;                       //!< Per-file plan rows: source, pack name, uri_to_pack, gltf_size, group, etc.
            Array m_plannedSidecarItems;                    //!< Sidecar work items: source, pack_name, true_path, is_bin, size.
            uint64_t m_plannedNewBytes;                     //!< Total bytes to copy; used for the disk check in buildImportPlan and checkDiskSpace.

        protected:
            /**
             * @brief Binds methods exposed to Godot.
            **/
            static void _bind_methods();

        public:

            /**
             * @brief Constructor of the node.
            **/
            Importer();

            /**
             * @brief Destructor.
            **/
            ~Importer();

            /**
             * @brief Called when the node enters the scene tree.
            **/
            void _ready();

            /**
             * @brief Drives the Gltf, sidecar, and deduplication sub-phases while the importer is in the Copying state.
             * @param p_delta Elapsed time since the previous frame.
             * @details Advances Gltf copy, then sidecar copy, then the deduplication and manifest write in SubDone, using one shared us budget for Gltf and sidecar in the same call.
            **/
            void _process(double p_delta);

            /**
             * @brief Loads the pack manifest, lists gltf files under the import path, builds the import plan, and starts copying.
             * @param p_assetPackPath Path to the asset pack root.
             * @param p_importAssetsPath Path to the folder of assets to import.
             * @return True if the manifest is valid, gltfs are found, the plan and disk check succeed, and the Copying state is entered.
            **/
            bool setupImportNewAssets(const String& p_assetPackPath, const String& p_importAssetsPath);

            /**
             * @brief Placeholder: import from an IIHexMap export bundle (not implemented).
             * @param p_importAssetsPath Reserved for future use.
             * @return Always false for now.
            **/
            bool setupImportIIHexMapExportContent(const String& p_importAssetsPath);

            /**
             * @brief Placeholder: repair a damaged pack (not implemented).
             * @param p_assetsPackPath Reserved for future use.
             * @return Always false for now.
            **/
            bool setupRepareAssetsPack(const String& p_assetsPackPath);

        private:

            /**
             * @brief Recursively lists gltf files under a path, up to FILE_MANIPULATION_MAX_DEEP.
             * @param p_importPath File or directory to scan.
             * @param p_gltfList In/out: absolute paths of glTF files.
             * @param p_currentDeep Current recursion depth.
             * @return True if the path was readable; false on depth limit or list failure.
            **/
            const bool assetsImporterCalulation(const String& p_importPath, Array& p_gltfList, int p_currentDeep);

            /**
             * @brief Fills the planned Gltf and sidecar work lists, then runs the disk space check.
             * @param p_gltfSourceList List of absolute paths to every gltf to import in this run.
             * @return True if a non-empty plan is built and there is enough disk space.
            **/
            const bool buildImportPlan(const Array& p_gltfSourceList);

            /**
             * @brief Resolves one uri, updates planned sidecar copies, or reuses a matching row in the manifest.
             * @param p_gltfSrc Absolute path to the source gltf file.
             * @param p_uri The uri string from the gltf JSON.
             * @param p_isBuffer True for buffers, false for images.
             * @param p_srcToPack In/out: maps resolved absolute file path to pack file name for this import run.
             * @param p_reserved In/out: names already taken for this run.
             * @return True after handling the uri; the current implementation does not return false.
            **/
            bool planOneSidecar(
                const String& p_gltfSrc, const String& p_uri, bool p_isBuffer, Dictionary& p_srcToPack, Dictionary& p_reserved);

            /**
             * @brief Returns whether the pack root has enough free space to copy the requested size (with a small margin when reported free space is tight).
             * @param p_needBytes Total byte size the import intends to add.
             * @return True if free space is unknown or large enough, false if definitely insufficient.
            **/
            const bool checkDiskSpace(uint64_t p_needBytes);

            /**
             * @brief Copies one glTF, rewrites uris in the JSON, and appends a row in assets_data and the capture list.
             * @param p_item One planned gltf item dictionary from the build step.
             * @return True on successful copy, rewrite, and manifest update.
            **/
            const bool copyGltfPlannedItem(const Dictionary& p_item);

            /**
             * @brief Inserts or updates a row in assets_data and the groups list when the group is new.
             * @param p_item Same shape as a planned item after copy (pack name, true_path, group, size, bin/texture dicts).
            **/
            void recordGltfRowInManifest(const Dictionary& p_item);

            /**
             * @brief Spends the shared per-frame time budget to copy a chunk of planned sidecar files.
             * @param io_timeBudgetUsec In/out: remaining time for this frame, in microseconds.
             * @return True while work may continue; false on copy failure.
            **/
            const bool runSidecarPhaseSlice(double& io_timeBudgetUsec);

            /**
             * @brief Sidecar and gltf deduplication, then recompute pack weights and write manifeste.json to disk.
             * @return True when dedup, recompute, and write succeed; always true on success in current implementation.
            **/
            const bool runDedupAndFinish();

            /**
             * @brief Merges duplicate bin_data and texture_data rows for the same on-disk file content.
            **/
            void deduplicateSidecarsInManifest();

            /**
             * @brief Deduplication pass for a single table name (bin_data or texture_data).
             * @param p_tableName Either bin_data or texture_data.
            **/
            void deduplicateOneSidecarTable(const String& p_tableName);

            /**
             * @brief Merges duplicate glTF in assets_data when true_path and sub-dictionaries match and file bytes match.
            **/
            void deduplicateGltfAssetsInManifest();

            /**
             * @brief Sums the weight field over assets_data, bin_data, and texture_data.
             * @return Total weight in bytes for all manifest row tables that define weight.
            **/
            uint64_t computeTotalWeightBytes();

            /**
             * @brief Refreshes poid_bytes, poid, and asset in the in-memory manifest.
            **/
            void recomputeGlobalSizesAndCount();

            /**
             * @brief Serializes the manifest to manifeste.json and sets the date field.
            **/
            void writeManifestToDisk();

            /**
             * @brief Ensures assets_data, bin_data, texture_data, and groups exist in the in-memory manifest.
            **/
            void ensureManifestDefaultTables();

            /**
             * @brief Returns whether the pack manifest has the required top-level keys.
             * @param p_manifest The dictionary loaded from manifeste.json.
             * @return True if all required keys are present.
            **/
            const bool isAssetsPackManifestValid(const Dictionary& p_manifest);

            /**
             * @brief Reads a JSON file from disk; returns a dictionary or an empty one on error.
             * @param p_path Full path to a text JSON file.
             * @return A dictionary on success, or an empty dictionary if missing, invalid JSON, or not a dict root.
            **/
            const Dictionary getDictionaryFromJsonPath(const String& p_path);

            /**
             * @brief Records every key of a dictionary in the reserved-name set for unique naming.
             * @param p_dict Table whose keys (file names) are added to the reserved set.
             * @param p_outReserved In/out: reserved set updated in place.
            **/
            void addDictionaryKeysToReserved(const Dictionary& p_dict, Dictionary& p_outReserved);

            /**
             * @brief Collects existing on-disk file names in the pack and keys from manifest tables to avoid name clashes.
             * @param p_reserved In/out: maps reserved name key to a truthy flag.
            **/
            void collectReservedNamesFromPack(Dictionary& p_reserved);

            /**
             * @brief Returns a path relative to the import root (forward slashes) or the file name only.
             * @param p_abs An absolute file path in the import space.
             * @return A relative true path string, or the file name only.
            **/
            const String toImportRootRelativePath(const String& p_abs);

            /**
             * @brief Subdirectory of the import tree used as the group field for a source glTF (may be empty).
             * @param p_srcGltfPath Absolute path to a gltf under the import root.
             * @return A relative subfolder for grouping, or an empty string for files at the import root.
            **/
            const String groupPathForSourceGltf(const String& p_srcGltfPath);

            /**
             * @brief Picks a file name that is not already in the reserved set, possibly with a numeric suffix.
             * @param p_preferred Preferred file name to try first.
             * @param p_reserved In/out: reserved set updated when a new name is taken.
             * @return A unique name registered in the reserved set.
            **/
            const String makeUniqueNameInSet(const String& p_preferred, Dictionary& p_reserved);

            /**
             * @brief True if a sidecar with the same true_path and size is already in the pack tables.
             * @param p_truePathRel True path of the file relative to the import root, forward slashes.
             * @param p_size File size in bytes to match the weight field.
             * @param p_isBin True to search bin_data, false to search texture_data.
             * @param p_outName Out: pack file name if a row matches; unchanged if false.
             * @return True if a matching row exists; false otherwise.
            **/
            const bool fileExistsInPackByTruePath(
                const String& p_truePathRel, uint64_t p_size, bool p_isBin, String& p_outName);

            /**
             * @brief Suggests a new pack file name (with extension) for a new sidecar, unique in the reserved set.
             * @param p_sourceAbs Absolute path to the source file on disk.
             * @param p_isBin True for a buffer, false for a texture.
             * @param p_size File size; kept for call symmetry (may be unused for naming).
             * @param p_reserved In/out: set of names already in use.
             * @return A new file name in the pack, unique in the reserved set.
            **/
            const String pickPackNameForNewSidecar(
                const String& p_sourceAbs, bool p_isBin, uint64_t p_size, Dictionary& p_reserved);

            /**
             * @brief Appends buffer or image uris from one root array in the glTF JSON when the uri maps to a local file.
             * @param p_root Parsed gltf root dictionary.
             * @param p_key Json key for the array, typically buffers or images.
             * @param p_isBuffer True to tag entries as buffer sidecars, false for images.
             * @param r_out In/out: array of small uri plan entries appended to the list.
            **/
            void appendGltfJsonSectionUris(const Dictionary& p_root, const char* p_key, bool p_isBuffer, Array& r_out);

            /**
             * @brief Fills a list of uri entries from the buffers and images lists, and optionally a sibling bin next to the gltf.
             * @param p_srcGltf Absolute path to the gltf.
             * @param p_root Parsed gltf root.
             * @param p_adjAbs Absolute path to a sibling co-located bin if present, else empty.
             * @param p_hasAdj True if p_adjAbs should be considered.
             * @param r_out In/out: uri and is_buffer list.
             * @return Always true; reserved for error propagation.
            **/
            const bool collectGltfUris(
                const String& p_srcGltf, const Dictionary& p_root, const String& p_adjAbs, bool p_hasAdj, Array& r_out);

            /**
             * @brief True for empty, data, or non-file uris.
             * @param p_uri A uri from gltf.
             * @return True if the uri cannot be mapped to a local file to copy.
            **/
            const bool gltfUriCannotMapToLocalFile(const String& p_uri);

            /**
             * @brief Resolves a glTF uri to an absolute path next to the source gltf file.
             * @param p_srcGltfPath Where the gltf lives on disk.
             * @param p_uri A uri from the gltf.
             * @return Absolute file path, or an empty string if the uri is not a local file reference.
            **/
            const String resolveGltfUriToAbsoluteFile(const String& p_srcGltfPath, const String& p_uri);

            /**
             * @brief Produces a JSON-quoted string token for search and replace in gltf text, same rules as JSON stringify.
             * @param p_unescaped File name or string as it should appear in JSON.
             * @return A quoted string token for byte-for-byte find in a gltf text.
            **/
            const String gltfJsonQuotedStringToken(const String& p_unescaped);

            /**
             * @brief Maps a manifest field enum to the Godot String used as a dictionary key.
             * @param p_key Which manifest key string to return.
             * @return The Godot String for use as a key in a Dictionary.
            **/
            const String manifest_key_gd(const E_ManifestJsonKey p_key) const;

            /**
             * @brief Deduplicates old-to-new name pairs, sorts by string length, then rewrites the file text in one pass.
             * @param p_text In/out: gltf file body as text, rewritten in place.
             * @param p_uriToName Maps each old uri to the pack file name to substitute.
            **/
            void sortDedupAndApplyUriStrings(String& p_text, const Dictionary& p_uriToName);

            /**
             * @brief True if two files have the same length and equal bytes for that length.
             * @param p_pathA First file path in the pack or elsewhere.
             * @param p_pathB Second file path in the pack or elsewhere.
             * @return True if both exist, same size, and content matches.
            **/
            const bool fileBinaryEqual(const String& p_pathA, const String& p_pathB);

            /**
             * @brief Shallow dictionary equality: same keys and operator== for each value.
             * @param p_a First dictionary.
             * @param p_b Second dictionary.
             * @return True if the two dictionaries are shallow-equal.
            **/
            const bool dictEqualShallow(const Dictionary& p_a, const Dictionary& p_b);

            /**
             * @brief Replaces a JSON-quoted old file name with a new one in a file when the old token is present.
             * @param p_path File to read and write.
             * @param p_oldName Old file base name in unescaped form, turned into a JSON token to find.
             * @param p_newName New name in unescaped form, turned into a JSON token to write.
             * @return True if read, optional replace, and write succeed.
            **/
            bool tryReplaceJsonQuotedStringInFile(const String& p_path, const String& p_oldName, const String& p_newName);

            void runPicturingPhase(double& io_timeBudgetUsec);

            bool isPicturingWorkDone();
    };
}
