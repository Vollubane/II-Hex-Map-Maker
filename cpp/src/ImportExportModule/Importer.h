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
 * @brief Time-sliced importer Node for pack import, selective removal, or pack repair.
 * @details Exactly one setup* call per instance; call queue_free() when the FSM stops or enters Destruct.
     */
    class Importer : public Node {
        GDCLASS(Importer, Node)

    private:

        //! Coarse operational mode driving the _process dispatch.
        enum class E_ImporterState {
            Waiting, Destruct, Copying, Picturing, RemovingAssets, RepairingPack,
        };

        //! Sub-phase sequencing within the Copying workflow.
        enum class E_CopySubPhase { Gltf, Sidecar, Dedup };

        //! Sub-phase sequencing within the RemovingAssets workflow.
        enum class E_RemoveSubPhase {
            ListedAssetsAndDrainDeletions, PruneSidecarsEnqueueDeletes,
            DrainAfterSidecarPrune, RecomputeAndWriteManifest, SubDone,
        };

        //! Sub-phase sequencing within the RepairingPack workflow.
        enum class E_RepairSubPhase {
            BuildOrphanGltfList, ResolveOrphanGltfSlice, CollectMissingThumbnailsList,
            PrepareDedupNonGltf, DedupCloneGroupsIterate, PrepareDedupGltfManifest,
            CollectDeletionCandidates, ExecuteDeletionQueue, BeginThumbnailGeneration,
            FinalizeRepairWrite, SubDone,
        };

        //! Pack-relative deletion backlog with deduplication guard.
        struct S_DeletionQueue {
            Array      queue; //!< Ordered list of pack-relative paths pending deletion.
            Dictionary seen;  //!< Set of already-enqueued paths to prevent duplicates.
        };

        //! Transient state owned by the Copying workflow.
        struct S_CopyCtx {
            E_CopySubPhase subPhase   = E_CopySubPhase::Gltf; //!< Current step within the copy pipeline.
            Array          plannedGltf;                        //!< Remaining glTF items to copy, each a Dictionary.
            Array          plannedSidecar;                     //!< Remaining sidecar items to copy, each a Dictionary.
            uint64_t       newBytes   = 0;                     //!< Estimated total bytes to be added to the pack.
        };

        //! Transient state owned by the Picturing (thumbnail) workflow.
        struct S_PicturingCtx {
            Array            toCapture;                                  //!< Absolute glTF paths awaiting thumbnail capture.
            Array            idle;                                       //!< ImporterPictureMaker instances ready to accept work.
            Array            active;                                     //!< ImporterPictureMaker instances currently rendering.
            bool             followsRepair = false;                      //!< True when picturing was spawned from a repair pass.
            E_RepairSubPhase returnPhase   = E_RepairSubPhase::SubDone;  //!< Phase to resume in RepairingPack after picturing finishes.
        };

        //! Transient state owned by the RemovingAssets workflow.
        struct S_RemoveCtx {
            E_RemoveSubPhase subPhase = E_RemoveSubPhase::SubDone; //!< Current step within the removal pipeline.
            S_DeletionQueue  deletions;                            //!< Deletion backlog accumulated during the remove pass.
            Array            gltfKeys;                             //!< Pack-relative glTF filenames scheduled for removal.
        };

        //! Transient state owned by the RepairingPack workflow.
        struct S_RepairCtx {
            E_RepairSubPhase subPhase        = E_RepairSubPhase::SubDone; //!< Current step within the repair pipeline.
            S_DeletionQueue  deletions;                                   //!< Deletion backlog accumulated during the repair pass.
            Array            workQueue;                                   //!< General-purpose per-step work list (orphan gltf names, etc.).
            Array            thumbnailQueue;                              //!< Absolute glTF paths needing a thumbnail after repair.
            Array            sizeBuckets;                                 //!< Candidate dedup groups, each a Dictionary with "names" and "_j".
            int64_t          dedupIdx        = 0;                         //!< Index into sizeBuckets currently being processed.
            bool             dedupIsGltfWave = false;                     //!< True during the glTF-manifest dedup wave, false for sidecar wave.
        };

        Dictionary       m_assetsPackManifest;         //!< Loaded root of manifeste.json, mutated in RAM.
        E_ImporterState  m_importerState;              //!< Current coarse FSM selector.
        String           m_assetPackPath;              //!< Absolute pack root on disk.
        String           m_importSourceRoot;           //!< Absolute import tree root for true_path derivation.
        Ref<PackedScene> m_importerPictureMakerScene;  //!< Cached scene resource used to instantiate ImporterPictureMaker nodes.
        double           m_timeBudget;                 //!< Microsecond budget remaining for the current frame slice.
        S_CopyCtx        m_copy;                       //!< Live context for the Copying workflow.
        S_PicturingCtx   m_picturing;                  //!< Live context for the Picturing workflow.
        S_RemoveCtx      m_remove;                     //!< Live context for the RemovingAssets workflow.
        S_RepairCtx      m_repair;                     //!< Live context for the RepairingPack workflow.

        protected:
            /**
             * @brief Registers GDScript-callable methods with the Godot ClassDB.
             */
            static void _bind_methods();

        public:
            /**
             * @brief Initialises member variables to safe defaults.
             */
            Importer();

            /**
             * @brief Default destructor.
             */
            ~Importer();

            /**
             * @brief Godot ready callback — loads and validates the ImporterPictureMaker scene resource.
             */
            void _ready();

            /**
             * @brief Godot process callback — advances the active FSM state by one time-sliced step.
             * @param p_delta Frame delta in seconds (unused; budget is derived from target FPS).
             */
            void _process(double p_delta);

            /**
             * @brief Prepares asynchronous import of glTF files discovered under p_importAssetsPath.
             * @param p_assetPackPath    Absolute path to the target asset pack root directory.
             * @param p_importAssetsPath Absolute path to the source tree containing glTF files.
             * @return False when prerequisites fail synchronously; the importer is queued for deletion.
             */
            bool setupImportNewAssets(const String& p_assetPackPath, const String& p_importAssetsPath);

            /**
             * @brief Validates manifest snapshot then drives integrity repair asynchronously.
             * @param p_assetsPackPath Absolute path to the asset pack root directory.
             * @return False when prerequisites fail synchronously; the importer is queued for deletion.
             */
            bool setupRepareAssetsPack(const String& p_assetsPackPath);

            /**
             * @brief Queues deterministic removal steps for the listed assets_data keys.
             * @param p_assetPackPath      Absolute path to the asset pack root directory.
             * @param p_packGltfFileNames  Array of String pack-relative glTF filenames to remove.
             * @return False when nothing schedulable remains or validation fails.
             */
            bool setupRemoveAssetsFromPack(const String& p_assetPackPath, const Array& p_packGltfFileNames);

        private:
            // ── Manifest helpers ──────────────────────────────────────────────────────

            /**
             * @brief Returns the assets_data Dictionary from the manifest, or an empty one.
             */
            const Dictionary manifestAssetsDict();

            /**
             * @brief Returns the bin_data Dictionary from the manifest, or an empty one.
             */
            const Dictionary manifestBinDict();

            /**
             * @brief Returns the texture_data Dictionary from the manifest, or an empty one.
             */
            const Dictionary manifestTexDict();

            /**
             * @brief Returns the groups Array from the manifest, or an empty one.
             */
            const Array manifestGroupsArray();

            /**
             * @brief Inserts default empty tables into the manifest if they are absent.
             */
            void ensureManifestDefaultTables();

            /**
             * @brief Returns true if the manifest contains the four required top-level keys.
             * @param p_manifest Dictionary to validate.
             */
            const bool isAssetsPackManifestValid(const Dictionary& p_manifest);

            /**
             * @brief Serialises m_assetsPackManifest to disk as pretty-printed JSON.
             * @return False on file-open failure.
             */
            const bool writeManifestToDisk();

            /**
             * @brief Optionally prunes empty groups, recomputes sizes, then writes the manifest.
             * @param p_prune_empty_groups Remove group entries no longer referenced by any asset.
             * @return False on write failure.
             */
            const bool commitPackManifestToDisk(bool p_prune_empty_groups);

            /**
             * @brief Loads and validates the manifest from disk; sets Destruct state on failure.
             * @param p_pack_root  Absolute path to the pack root directory.
             * @param p_context    Human-readable context string used in error messages.
             * @return False if the file is missing, unparseable, or lacks required keys.
             */
            const bool loadPackManifestFromDiskOrDestruct(const String& p_pack_root, const String& p_context);

            /**
             * @brief Reads and parses a JSON file, returning its root Dictionary.
             * @param p_path Absolute path to the JSON file.
             * @return Empty Dictionary on any failure (missing file, parse error, wrong type).
             */
            const Dictionary getDictionaryFromJsonPath(const String& p_path);

            /**
             * @brief Deduplicates sidecar tables and glTF assets, then commits the manifest.
             * @param p_prune_empty_groups Forwarded to commitPackManifestToDisk.
             * @return False on write failure.
             */
            const bool runFullManifestSidecarAndGltfDedupCommit(bool p_prune_empty_groups);

            /**
             * @brief Updates poid_bytes, poid (human-readable MB string), and asset count in the manifest.
             */
            void recomputeGlobalSizesAndCount();

            /**
             * @brief Sums weight bytes across all three manifest tables.
             * @return Total size in bytes.
             */
            const uint64_t computeTotalWeightBytes();

            /**
             * @brief Removes group entries from the manifest that are not referenced by any asset row.
             */
            void pruneEmptyGroupsInManifest();

            /**
             * @brief Merges duplicate sidecar entries in one table using binary file equality.
             * @param p_table_name Manifest key of the table to process (BIN_DATA_KEY or TEX_DATA_KEY).
             */
            void deduplicateOneSidecarTable(const String& p_table_name);

            /**
             * @brief Iteratively merges duplicate glTF asset entries in the manifest using content equality.
             */
            void deduplicateGltfAssetsInManifest();

            /**
             * @brief Splits an asset row into its embedded bin and texture sidecar maps.
             * @param p_row      Source asset row Dictionary.
             * @param p_out_bin  Receives the bin sub-Dictionary.
             * @param p_out_tex  Receives the texture sub-Dictionary.
             */
            void extractBinTexMapsFromAssetRow(const Dictionary& p_row, Dictionary& p_out_bin, Dictionary& p_out_tex);

            /**
             * @brief Populates p_out_allow with all pack filenames referenced by the manifest.
             * @param p_out_allow Output Dictionary used as a set (key = filename, value = true).
             */
            void buildAllowedReferencedPackFilenames(Dictionary& p_out_allow);

            /**
             * @brief Builds a stable fingerprint string for a sidecar sub-Dictionary.
             * @param p_dict Dictionary whose entries are sorted and serialised.
             * @return Canonical string of the form "key=value;…".
             */
            const String fingerprintManifestSidecarDict(const Dictionary& p_dict);

            /**
             * @brief Inserts a sidecar row into the given manifest table if the key is absent.
             * @param p_table      Manifest key of the target table (BIN_DATA_KEY or TEX_DATA_KEY).
             * @param p_key        Pack-relative filename used as the row key.
             * @param p_true_path  Relative import-source path stored in the row.
             * @param p_weight     File size in bytes.
             */
            void ensureManifestSidecarRow(const String& p_table, const String& p_key, const String& p_true_path, int64_t p_weight);

            /**
             * @brief Builds a minimal sidecar row Dictionary with true_path and weight fields.
             * @param p_true_path Relative import-source path.
             * @param p_weight    File size in bytes.
             * @return New sidecar row Dictionary.
             */
            const Dictionary makeSidecarRow(const String& p_true_path, int64_t p_weight);

            /**
             * @brief Writes an asset row into the manifest's assets_data table and updates the groups list.
             * @param p_item Dictionary describing the imported glTF (pack_name, true_path, group, gltf_size, bin, texture).
             */
            void recordGltfRowInManifest(const Dictionary& p_item);

            // ── Setup helpers ─────────────────────────────────────────────────────────

            /**
             * @brief Logs p_msg as an error, enters Destruct state, queues deletion, and returns false.
             * @param p_msg Error message to emit.
             * @return Always false.
             */
            const bool failSetup(const String& p_msg);

            // ── Copy phase ────────────────────────────────────────────────────────────

            /**
             * @brief Recursively collects .gltf file paths under p_import_path up to the depth limit.
             * @param p_import_path  Absolute path to scan (may be a file or directory).
             * @param p_gltf_list    Accumulator Array receiving absolute .gltf paths.
             * @param p_current_deep Current recursion depth, checked against FILE_MANIPULATION_MAX_DEEP.
             * @return False if depth is exceeded or a directory cannot be opened.
             */
            const bool enumerateGltfUnderImportPath(const String& p_import_path, Array& p_gltf_list, int p_current_deep);

            /**
             * @brief Builds m_copy.plannedGltf and m_copy.plannedSidecar from a list of source glTF paths.
             * @param p_gltf_source_list Array of absolute .gltf source paths to plan.
             * @return False on IO error, JSON parse failure, or insufficient disk space.
             */
            const bool buildImportPlan(const Array& p_gltf_source_list);

            /**
             * @brief Plans the copy of one sidecar referenced by a glTF URI entry.
             * @param p_gltf_src    Absolute path to the owning .gltf file.
             * @param p_uri_row     Dictionary with "uri" and "is_buffer" fields.
             * @param p_src_to_pack Accumulated source→pack-name mapping (in/out).
             * @param p_reserved    Set of already-reserved pack filenames (in/out).
             * @return False on IO error.
             */
            const bool planOneSidecar(const String& p_gltf_src, const Dictionary& p_uri_row, Dictionary& p_src_to_pack, Dictionary& p_reserved);

            /**
             * @brief Checks whether the pack volume has sufficient free space for the estimated import.
             * @param p_need_bytes Estimated bytes required.
             * @return False if disk space is insufficient.
             */
            const bool checkDiskSpace(uint64_t p_need_bytes);

            /**
             * @brief Copies a planned glTF item to the pack and records it in the manifest.
             * @param p_item Dictionary produced by buildImportPlan describing one glTF.
             * @return False on file copy failure.
             */
            const bool copyGltfPlannedItem(const Dictionary& p_item);

            /**
             * @brief Drains m_copy.plannedSidecar within the current time budget.
             * @return False on copy failure; advances subPhase to Dedup when the list is empty.
             */
            const bool runSidecarPhaseSlice();

            /**
             * @brief Runs sidecar and glTF deduplication, writes the manifest, then transitions to Picturing.
             * @return False if the manifest write fails.
             */
            const bool runDedupAndFinish();

            /**
             * @brief Advances the copy FSM (Gltf → Sidecar → Dedup) within the frame budget.
             */
            void progressImportCopyStateForFrame();

            /**
             * @brief Copies a sidecar file to the pack and records or updates its manifest row.
             * @param p_source    Absolute source path.
             * @param p_pack_name Pack-relative destination filename.
             * @param p_true_path Relative import-source path stored in the manifest.
             * @param p_is_bin    True for .bin buffer, false for texture.
             * @param p_size      File size in bytes.
             * @return False on file copy failure.
             */
            const bool copySidecarToPackAndRecord(const String& p_source, const String& p_pack_name, const String& p_true_path, bool p_is_bin, int64_t p_size);

            // ── Picturing phase ───────────────────────────────────────────────────────

            /**
             * @brief Advances thumbnail capture: spawns workers, ticks active ones, dispatches queued paths.
             */
            void runPicturingPhase();

            // ── Remove phase ──────────────────────────────────────────────────────────

            /**
             * @brief Drives the RemovingAssets FSM sub-phases within the frame budget.
             */
            void runRemovingAssetsSlice();

            /**
             * @brief Enqueues a pack-relative path into the remove-phase deletion backlog.
             * @param p_rel Pack-relative path to delete.
             */
            void removeEnqueueDeletionRel(const String& p_rel);

            /**
             * @brief Removes sidecar manifest rows with zero asset references and enqueues their files.
             * @param p_dq Deletion queue to receive unreferenced sidecar filenames.
             */
            void pruneUnreferencedSidecarsAndEnqueue(S_DeletionQueue& p_dq);

            // ── Repair phase ──────────────────────────────────────────────────────────

            /**
             * @brief Drives the RepairingPack FSM sub-phases within the frame budget.
             */
            void runRepairPackSlice();

            /**
             * @brief Enqueues a pack-relative path into the repair-phase deletion backlog.
             * @param p_rel Pack-relative path to delete.
             */
            void repairEnqueueDeletionRel(const String& p_rel);

            /**
             * @brief Attempts to integrate an orphan glTF (on disk but absent from manifest) into the manifest.
             * @param p_pack_gltf_name Pack-relative filename of the orphan glTF.
             * @return True (always continues repair); the file is enqueued for deletion if unrecoverable.
             */
            const bool repairTryAdoptOrphanGltf(const String& p_pack_gltf_name);

            /**
             * @brief Populates m_repair.thumbnailQueue with glTF paths that are missing their capture PNG.
             */
            void repairFillThumbnailQueueFromManifest();

            /**
             * @brief Builds m_repair.sizeBuckets grouping non-glTF pack files by extension and size.
             */
            void repairBuildNonGltfExtensionSizeBuckets();

            /**
             * @brief Builds m_repair.sizeBuckets grouping glTF entries by weight and sidecar fingerprint.
             */
            void repairBuildManifestGltfWeightSideBuckets();

            /**
             * @brief Processes one dedup step across m_repair.sizeBuckets.
             * @return True while work remains in the current bucket set; false when all buckets are exhausted.
             */
            const bool repairDedupCloneGroupsOneStep();

            /**
             * @brief Collects unreferenced pack files and stale capture PNGs into the repair deletion queue.
             */
            void repairCollectDeletionCandidates();

            /**
             * @brief Transitions to Picturing state if thumbnails are pending, otherwise finalises repair directly.
             */
            void repairBeginThumbnailGeneration();

            /**
             * @brief Final repair step: runs full dedup and writes the manifest to disk.
             */
            void repairFinalizeAfterRepairWrite();

            /**
             * @brief Updates only the _j field of a size bucket in-place.
             * @param p_bucket Bucket Dictionary to update.
             * @param p_jj     New value for the _j iterator field.
             */
            void repairDedupPersistJjOnly(Dictionary& p_bucket, int64_t p_jj);

            /**
             * @brief Writes updated names and _j back into a size bucket in-place.
             * @param p_bucket Bucket Dictionary to update.
             * @param p_names  Updated names Array.
             * @param p_jj     New value for the _j iterator field.
             */
            void repairDedupPersistBucket(Dictionary& p_bucket, const Array& p_names, int64_t p_jj);

            /**
             * @brief Merges two duplicate glTF pack files by removing p_drop from the manifest and from disk.
             * @param p_keep          Pack-relative name of the file to retain.
             * @param p_drop          Pack-relative name of the file to discard.
             * @param p_defer_delete  If true, enqueues deletion; otherwise deletes immediately.
             * @param p_already_equal Skip the binary equality check (caller guarantees equality).
             * @return False if either file is absent from the manifest or the files are not binary-equal.
             */
            const bool mergeRepairDuplicateGltfPackFiles(const String& p_keep, const String& p_drop, bool p_defer_delete, bool p_already_equal);

            /**
             * @brief Redirects all asset references from p_drop to p_keep in one sidecar table.
             * @param p_table        Manifest table key (BIN_DATA_KEY or TEX_DATA_KEY).
             * @param p_keep         Sidecar filename to retain.
             * @param p_drop         Sidecar filename to discard.
             * @param p_defer_delete If true, enqueues deletion; otherwise deletes immediately.
             */
            void mergeRepairPackSidecarsInTable(const String& p_table, const String& p_keep, const String& p_drop, bool p_defer_delete);

            /**
             * @brief Fills p_out_base_names with the .gltf base filenames found at the pack root.
             * @param p_out_base_names Output Array receiving base filenames.
             */
            void listPackRootGltfFileNames(Array& p_out_base_names);

            // ── Shared low-level helpers ──────────────────────────────────────────────

            /**
             * @brief Adds a normalised pack-relative path to a deletion queue, guarding against duplicates.
             * @param p_dq  Deletion queue to append to.
             * @param p_rel Raw pack-relative path (may contain backslashes or a leading slash).
             */
            void enqueuePackRelativeDeletionRel(S_DeletionQueue& p_dq, const String& p_rel);

            /**
             * @brief Deletes files from p_dq.queue one by one within the remaining time budget.
             * @param p_dq Deletion queue to drain.
             */
            void drainPackRelativeDeletionQueueSlice(S_DeletionQueue& p_dq);

            /**
             * @brief Deletes a single file identified by its pack-relative path.
             * @param p_rel_pack_path Pack-relative path (leading slashes and backslashes are normalised).
             */
            void deletePackRelativeFile(const String& p_rel_pack_path);

            /**
             * @brief Lists all files (non-recursive) in a directory.
             * @param p_abs_path      Absolute directory path to enumerate.
             * @param p_out_filenames Output Array receiving base filenames (no directory prefix).
             */
            void enumerateFilesInDirNonRecursive(const String& p_abs_path, Array& p_out_filenames);

            /**
             * @brief Subtracts elapsed microseconds since p_slice_start_usec from m_timeBudget.
             * @param p_slice_start_usec Tick value captured before the work slice began.
             */
            void debitTimeBudgetFromTicks(uint64_t p_slice_start_usec);

            /**
             * @brief Performs a chunk-by-chunk binary comparison of two files.
             * @param p_path_a Absolute path to the first file.
             * @param p_path_b Absolute path to the second file.
             * @return True only if both files exist, have the same length, and are byte-identical.
             */
            const bool fileBinaryEqual(const String& p_path_a, const String& p_path_b);

            /**
             * @brief Shallow-compares two Dictionaries: same keys and Variant-equal values.
             * @param p_a First Dictionary.
             * @param p_b Second Dictionary.
             * @return True if both Dictionaries have identical key/value pairs.
             */
            const bool dictEqualShallow(const Dictionary& p_a, const Dictionary& p_b);

            /**
             * @brief Replaces a JSON-quoted string literal in a text file if it appears at least once.
             * @param p_path Path to the file to modify in-place.
             * @param p_old  Unquoted original string value.
             * @param p_new  Unquoted replacement string value.
             * @return False on file IO failure; true if unchanged or successfully replaced.
             */
            const bool tryReplaceJsonQuotedStringInFile(const String& p_path, const String& p_old, const String& p_new);

            /**
             * @brief Returns true if p_abs_path is equal to or is a child of m_assetPackPath.
             * @param p_abs_path Absolute path to test.
             */
            const bool pathIsUnderPack(const String& p_abs_path);

            /**
             * @brief Computes the path of p_abs relative to m_importSourceRoot.
             * @param p_abs Absolute path to convert.
             * @return Relative path string, or just the filename if p_abs is outside the root.
             */
            const String toImportRootRelativePath(const String& p_abs);

            /**
             * @brief Derives the group subdirectory for a source glTF relative to the import root.
             * @param p_src_gltf_path Absolute path to the source .gltf file.
             * @return Group path string, empty if the file is directly under the import root.
             */
            const String groupPathForSourceGltf(const String& p_src_gltf_path);

            /**
             * @brief Returns p_preferred if not reserved, otherwise appends an incrementing _N suffix.
             * @param p_preferred Desired filename.
             * @param p_reserved  Set of already-taken names (in/out); the winner is added to the set.
             * @return A unique filename not present in p_reserved.
             */
            const String makeUniqueNameInSet(const String& p_preferred, Dictionary& p_reserved);

            /**
             * @brief Chooses a unique pack filename for a sidecar to be copied.
             * @param p_source_abs Absolute source path.
             * @param p_is_bin     True for .bin buffer, false for texture.
             * @param p_size       File size in bytes (reserved for future bucketing, currently unused).
             * @param p_reserved   Set of already-taken pack names (in/out).
             * @return Unique pack-relative base filename for the sidecar.
             */
            const String pickPackNameForNewSidecar(const String& p_source_abs, bool p_is_bin, uint64_t p_size, Dictionary& p_reserved);

            /**
             * @brief Searches the appropriate sidecar table for an entry matching true_path and size.
             * @param p_true_path_rel Relative import-source path to match.
             * @param p_size          File size in bytes to match.
             * @param p_is_bin        Selects the bin or texture table.
             * @param p_out_name      Receives the matching pack filename on success.
             * @return True if a matching row is found.
             */
            const bool fileExistsInPackByTruePath(const String& p_true_path_rel, uint64_t p_size, bool p_is_bin, String& p_out_name);

            /**
             * @brief Populates p_reserved with all pack filenames that must not be overwritten.
             * @param p_reserved Output Dictionary (key = filename, value = true).
             * @details Covers keys from all three manifest tables plus files present on disk.
             */
            void collectReservedNamesFromPack(Dictionary& p_reserved);

            /**
             * @brief Appends URI descriptor rows for all buffers and images found in a glTF root.
             * @param p_root         Parsed glTF root Dictionary.
             * @param p_out_uri_rows Output Array; each entry is a Dictionary with "uri" and "is_buffer".
             */
            void appendGltfBuffersAndImagesUriRows(const Dictionary& p_root, Array& p_out_uri_rows);

            /**
             * @brief Returns true if the URI cannot be resolved to a local file (data URL, http, etc.).
             * @param p_uri URI string from the glTF JSON.
             */
            const bool gltfUriCannotMapToLocalFile(const String& p_uri);

            /**
             * @brief Resolves a glTF-relative URI to an absolute filesystem path.
             * @param p_src_gltf_path Absolute path to the owning .gltf file.
             * @param p_uri           URI string from the glTF JSON.
             * @return Absolute path, or empty string for non-local URIs.
             */
            const String resolveGltfUriToAbsoluteFile(const String& p_src_gltf_path, const String& p_uri);

            /**
             * @brief Deduplicates, sorts by descending URI length, and applies all URI replacements to p_text.
             * @param p_text        Raw glTF JSON text to modify in-place.
             * @param p_uri_to_name Map from original URI strings to their new pack filenames.
             */
            void sortDedupAndApplyUriStrings(String& p_text, const Dictionary& p_uri_to_name);

            /**
             * @brief Returns the pack-relative path of the capture PNG for a given glTF pack filename.
             * @param p_gltf_filename Pack-relative glTF filename (e.g. "model.gltf").
             * @return Path of the form "capture/<basename>.png".
             */
            const String packRelativeCapturePngForGltfPackName(const String& p_gltf_filename);

            /**
             * @brief Safely reads an int64_t from a Dictionary, returning 0 if the key is absent.
             * @param p_dict Source Dictionary.
             * @param p_key  Key to look up.
             * @return Integer value, or 0 if absent.
             */
            const int64_t dictGetInt(const Dictionary& p_dict, const Variant& p_key);

            /**
             * @brief Appends p_val to an Array stored at p_key in p_dict, creating the Array if needed.
             * @param p_dict Target Dictionary.
             * @param p_key  String key whose value must be (or will become) an Array.
             * @param p_val  Value to append.
             */
            void dictPushToArray(Dictionary& p_dict, const String& p_key, const Variant& p_val);
    };
}
