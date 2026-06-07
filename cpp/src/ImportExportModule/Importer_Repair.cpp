#include "Importer.h"
#include "Importer_Constants.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace ImportExportModule {

    void Importer::runPicturingPhase() {
        _emitLoading("Generating Thumbnails", "Rendering thumbnails",
            String::num_int64(m_picturing.toCapture.size()) + String(" to capture (")
            + String::num_int64(m_picturing.active.size()) + String(" active)"));
        if(m_importerPictureMakerScene.is_null()) return;
        while(m_picturing.idle.size() + m_picturing.active.size() < PICTURE_MAKER_MAX && 0.0 < m_timeBudget) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            Node* newPictureMakerNode = m_importerPictureMakerScene->instantiate();
            if(!newPictureMakerNode) { UtilityFunctions::push_error("Importer: picturing, instantiate ImporterPictureMaker failed"); break; }
            ImporterPictureMaker* newPictureMaker = Object::cast_to<ImporterPictureMaker>(newPictureMakerNode);
            if(!newPictureMaker) {
                UtilityFunctions::push_error(String("Importer: picturing, scene root is not ImporterPictureMaker: ") + newPictureMakerNode->get_class());
                newPictureMakerNode->queue_free(); debitTimeBudgetFromTicks(t0); break;
            }
            add_child(newPictureMaker);
            m_picturing.idle.append(newPictureMaker);
            debitTimeBudgetFromTicks(t0);
        }
        for(int i = m_picturing.active.size() - 1; 0 <= i && 0.0 < m_timeBudget; --i) {
            Object* activeObject = m_picturing.active[i].operator Object*();
            if(!activeObject) {
                UtilityFunctions::push_error(String("Importer: picturing, null in active at index ") + String::num_int64(i));
                m_picturing.active.remove_at(i); continue;
            }
            ImporterPictureMaker* activePictureMaker = Object::cast_to<ImporterPictureMaker>(activeObject);
            if(!activePictureMaker) {
                UtilityFunctions::push_error(String("Importer: picturing, active[") + String::num_int64(i)
                    + String("] is not ImporterPictureMaker: ") + activeObject->get_class());
                if(Node* invalidActiveNode = Object::cast_to<Node>(activeObject)) invalidActiveNode->queue_free();
                m_picturing.active.remove_at(i); continue;
            }
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const ImporterPictureMaker::E_ImporterPictureMakerState picturingState = activePictureMaker->stepProgress();
            debitTimeBudgetFromTicks(t0);
            if(picturingState == ImporterPictureMaker::E_ImporterPictureMakerState::Waiting) {
                m_picturing.active.remove_at(i);
                m_picturing.idle.append(activePictureMaker);
            }
        }
        while(0.0 < m_timeBudget && 0 < m_picturing.toCapture.size() && 0 < m_picturing.idle.size()) {
            const uint64_t t0       = Time::get_singleton()->get_ticks_usec();
            const int lastIdleIndex = m_picturing.idle.size() - 1;
            Object* idleObject = m_picturing.idle[lastIdleIndex].operator Object*();
            if(!idleObject) {
                UtilityFunctions::push_error(String("Importer: picturing, null in idle at index ") + String::num_int64(lastIdleIndex));
                m_picturing.idle.remove_at(lastIdleIndex); debitTimeBudgetFromTicks(t0); continue;
            }
            ImporterPictureMaker* idlePictureMaker = Object::cast_to<ImporterPictureMaker>(idleObject);
            if(!idlePictureMaker) {
                UtilityFunctions::push_error(String("Importer: picturing, idle[") + String::num_int64(lastIdleIndex)
                    + String("] is not ImporterPictureMaker: ") + idleObject->get_class());
                if(Node* invalidIdleNode = Object::cast_to<Node>(idleObject)) invalidIdleNode->queue_free();
                m_picturing.idle.remove_at(lastIdleIndex); debitTimeBudgetFromTicks(t0); continue;
            }
            m_picturing.idle.remove_at(lastIdleIndex);
            const String gltfAbsPath = m_picturing.toCapture[0];
            m_picturing.toCapture.remove_at(0);
            idlePictureMaker->makeAPicture(gltfAbsPath);
            m_picturing.active.append(idlePictureMaker);
            debitTimeBudgetFromTicks(t0);
        }
    }

    void Importer::runRemovingAssetsSlice() {
        switch(m_remove.subPhase) {
            case E_RemoveSubPhase::ListedAssetsAndDrainDeletions:
                if(!m_remove.deletions.queue.is_empty())
                    _emitLoading("Removing Assets", "Deleting files",
                        String::num_int64(m_remove.deletions.queue.size()) + String(" left"));
                else
                    _emitLoading("Removing Assets", "Removing asset files",
                        String::num_int64(m_remove.gltfKeys.size()) + String(" asset(s) left"));
                break;
            case E_RemoveSubPhase::PruneSidecarsEnqueueDeletes:
                _emitLoading("Removing Assets", "Pruning sidecars", ""); break;
            case E_RemoveSubPhase::DrainAfterSidecarPrune:
                _emitLoading("Removing Assets", "Deleting files",
                    String::num_int64(m_remove.deletions.queue.size()) + String(" left"));
                break;
            case E_RemoveSubPhase::RecomputeAndWriteManifest:
                _emitLoading("Removing Assets", "Writing manifest", ""); break;
            default: break;
        }
        while(0.0 < m_timeBudget && m_remove.subPhase != E_RemoveSubPhase::SubDone) {
            switch(m_remove.subPhase) {
                case E_RemoveSubPhase::ListedAssetsAndDrainDeletions: {
                    if(!m_remove.deletions.queue.is_empty()) {
                        drainPackRelativeDeletionQueueSlice(m_remove.deletions);
                        break;
                    }
                    if(!m_remove.gltfKeys.is_empty()) {
                        const uint64_t t0         = Time::get_singleton()->get_ticks_usec();
                        const String gltfPackName = String(m_remove.gltfKeys[0]);
                        m_remove.gltfKeys.remove_at(0);
                        Dictionary assetsDict = manifestAssetsDict();
                        if(assetsDict.has(Variant(gltfPackName))) {
                            assetsDict.erase(Variant(gltfPackName));
                            m_assetsPackManifest[ASSETS_KEY] = assetsDict;
                        }
                        removeEnqueueDeletionRel(gltfPackName);
                        removeEnqueueDeletionRel(packRelativeCapturePngForGltfPackName(gltfPackName));
                        debitTimeBudgetFromTicks(t0);
                        break;
                    }
                    m_remove.subPhase = E_RemoveSubPhase::PruneSidecarsEnqueueDeletes;
                } break;

                case E_RemoveSubPhase::PruneSidecarsEnqueueDeletes: {
                    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
                    pruneUnreferencedSidecarsAndEnqueue(m_remove.deletions);
                    m_remove.subPhase = E_RemoveSubPhase::DrainAfterSidecarPrune;
                    debitTimeBudgetFromTicks(t0);
                } break;

                case E_RemoveSubPhase::DrainAfterSidecarPrune: {
                    if(!m_remove.deletions.queue.is_empty())
                        drainPackRelativeDeletionQueueSlice(m_remove.deletions);
                    else
                        m_remove.subPhase = E_RemoveSubPhase::RecomputeAndWriteManifest;
                } break;

                case E_RemoveSubPhase::RecomputeAndWriteManifest: {
                    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
                    commitPackManifestToDisk(true);
                    m_remove.subPhase = E_RemoveSubPhase::SubDone;
                    debitTimeBudgetFromTicks(t0);
                } break;

                default:
                    m_remove.subPhase = E_RemoveSubPhase::SubDone;
                    break;
            }
        }
    }

    void Importer::pruneUnreferencedSidecarsAndEnqueue(S_DeletionQueue& p_dq) {
        Dictionary refCounts;
        const Dictionary assetsDict = manifestAssetsDict();
        const Array assetKeys = assetsDict.keys();
        for(int ai = 0; ai < assetKeys.size(); ++ai) {
            if(!assetsDict.has(assetKeys[ai])) continue;
            Dictionary assetBinSidecars, assetTexSidecars;
            extractBinTexMapsFromAssetRow(Dictionary(assetsDict[assetKeys[ai]]), assetBinSidecars, assetTexSidecars);
            const Array binSidecarFilenames = assetBinSidecars.keys();
            for(int bi = 0; bi < binSidecarFilenames.size(); ++bi) {
                const String refCountKey = String("b/") + String(binSidecarFilenames[bi]);
                refCounts[refCountKey] = dictGetInt(refCounts, refCountKey) + 1;
            }
            const Array texSidecarFilenames = assetTexSidecars.keys();
            for(int ti = 0; ti < texSidecarFilenames.size(); ++ti) {
                const String refCountKey = String("t/") + String(texSidecarFilenames[ti]);
                refCounts[refCountKey] = dictGetInt(refCounts, refCountKey) + 1;
            }
        }
        for(int pass = 0; pass < 2; ++pass) {
            const char* const sidecarTableName = pass == 0 ? BIN_DATA_KEY : TEX_DATA_KEY;
            if(!m_assetsPackManifest.has(sidecarTableName)) continue;
            Dictionary sidecarTable = Dictionary(m_assetsPackManifest[sidecarTableName]);
            const Array sidecarFilenames = sidecarTable.keys();
            for(int i = 0; i < sidecarFilenames.size(); ++i) {
                const String sidecarFilename = String(sidecarFilenames[i]);
                if(!sidecarTable.has(Variant(sidecarFilename))) continue;
                const String refCountLookupKey = (pass == 0 ? String("b/") : String("t/")) + sidecarFilename;
                if(dictGetInt(refCounts, refCountLookupKey) == 0LL) {
                    sidecarTable.erase(Variant(sidecarFilename));
                    enqueuePackRelativeDeletionRel(p_dq, sidecarFilename);
                }
            }
            m_assetsPackManifest[sidecarTableName] = sidecarTable;
        }
    }

    void Importer::removeEnqueueDeletionRel(const String& p_rel) {
        enqueuePackRelativeDeletionRel(m_remove.deletions, p_rel);
    }

    void Importer::runRepairPackSlice() {
        switch(m_repair.subPhase) {
            case E_RepairSubPhase::BuildOrphanGltfList:
                _emitLoading("Repairing Pack", "Scanning orphan files", ""); break;
            case E_RepairSubPhase::ResolveOrphanGltfSlice:
                _emitLoading("Repairing Pack", "Adopting orphan files",
                    String::num_int64(m_repair.workQueue.size()) + String(" left"));
                break;
            case E_RepairSubPhase::PrepareDedupNonGltf:
                _emitLoading("Repairing Pack", "Preparing deduplication", ""); break;
            case E_RepairSubPhase::DedupCloneGroupsIterate:
                _emitLoading("Repairing Pack",
                    m_repair.dedupIsGltfWave ? String("Deduplicating glTF") : String("Deduplicating sidecars"),
                    String("Bucket ") + String::num_int64(m_repair.dedupIdx + 1)
                        + String(" / ") + String::num_int64(m_repair.sizeBuckets.size()));
                break;
            case E_RepairSubPhase::PrepareDedupGltfManifest:
                _emitLoading("Repairing Pack", "Preparing glTF dedup", ""); break;
            case E_RepairSubPhase::CollectDeletionCandidates:
                _emitLoading("Repairing Pack", "Collecting duplicates", ""); break;
            case E_RepairSubPhase::ExecuteDeletionQueue:
                _emitLoading("Repairing Pack", "Deleting duplicates",
                    String::num_int64(m_repair.deletions.queue.size()) + String(" file(s)"));
                break;
            case E_RepairSubPhase::BeginThumbnailGeneration:
                _emitLoading("Repairing Pack", "Starting thumbnails", ""); break;
            case E_RepairSubPhase::FinalizeRepairWrite:
                _emitLoading("Repairing Pack", "Writing manifest", ""); break;
            default: break;
        }
        while(0.0 < m_timeBudget) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            bool elapsedCharged = false;
            switch(m_repair.subPhase) {
                case E_RepairSubPhase::BuildOrphanGltfList: {
                    m_repair.workQueue.clear();
                    Array diskGltfFilenames;
                    listPackRootGltfFileNames(diskGltfFilenames);
                    const Dictionary assetsDict = manifestAssetsDict();
                    for(int i = 0; i < diskGltfFilenames.size(); ++i) {
                        const String diskGltfName = String(diskGltfFilenames[i]);
                        if(diskGltfName.is_empty() || diskGltfName == "." || diskGltfName == "..") continue;
                        if(!assetsDict.has(Variant(diskGltfName))) m_repair.workQueue.append(diskGltfName);
                    }
                    m_repair.subPhase = E_RepairSubPhase::ResolveOrphanGltfSlice;
                } break;

                case E_RepairSubPhase::ResolveOrphanGltfSlice: {
                    if(0.0 < m_timeBudget && !m_repair.workQueue.is_empty()) {
                        const String orphanGltfName = String(m_repair.workQueue[0]);
                        m_repair.workQueue.remove_at(0);
                        repairTryAdoptOrphanGltf(orphanGltfName);
                    }
                    if(m_repair.workQueue.is_empty())
                        m_repair.subPhase = E_RepairSubPhase::CollectMissingThumbnailsList;
                } break;

                case E_RepairSubPhase::CollectMissingThumbnailsList:
                    m_repair.subPhase = E_RepairSubPhase::PrepareDedupNonGltf;
                    break;

                case E_RepairSubPhase::PrepareDedupNonGltf:
                    repairBuildNonGltfExtensionSizeBuckets();
                    m_repair.dedupIdx        = 0;
                    m_repair.dedupIsGltfWave = false;
                    m_repair.subPhase        = E_RepairSubPhase::DedupCloneGroupsIterate;
                    break;

                case E_RepairSubPhase::DedupCloneGroupsIterate: {
                    elapsedCharged = true;
                    bool more = true;
                    while(0.0 < m_timeBudget && more) more = repairDedupCloneGroupsOneStep();
                    if(!more) {
                        m_repair.subPhase  = m_repair.dedupIsGltfWave
                            ? E_RepairSubPhase::CollectDeletionCandidates
                            : E_RepairSubPhase::PrepareDedupGltfManifest;
                        m_repair.dedupIdx  = 0;
                    }
                } break;

                case E_RepairSubPhase::PrepareDedupGltfManifest:
                    repairBuildManifestGltfWeightSideBuckets();
                    m_repair.dedupIdx        = 0;
                    m_repair.dedupIsGltfWave = true;
                    m_repair.subPhase        = E_RepairSubPhase::DedupCloneGroupsIterate;
                    break;

                case E_RepairSubPhase::CollectDeletionCandidates:
                    repairCollectDeletionCandidates();
                    m_repair.subPhase = E_RepairSubPhase::ExecuteDeletionQueue;
                    break;

                case E_RepairSubPhase::ExecuteDeletionQueue:
                    elapsedCharged = true;
                    drainPackRelativeDeletionQueueSlice(m_repair.deletions);
                    if(m_repair.deletions.queue.is_empty())
                        m_repair.subPhase = E_RepairSubPhase::BeginThumbnailGeneration;
                    break;

                case E_RepairSubPhase::BeginThumbnailGeneration:
                    repairBeginThumbnailGeneration();
                    debitTimeBudgetFromTicks(t0);
                    return;

                case E_RepairSubPhase::FinalizeRepairWrite:
                    repairFinalizeAfterRepairWrite();
                    m_repair.subPhase = E_RepairSubPhase::SubDone;
                    break;

                default:
                    m_repair.subPhase = E_RepairSubPhase::SubDone;
                    break;
            }
            if(!elapsedCharged) m_timeBudget -= double(Time::get_singleton()->get_ticks_usec() - t0);
            if((Time::get_singleton()->get_ticks_usec() - t0) == 0 || m_importerState == E_ImporterState::Picturing) break;
        }
    }

    void Importer::repairEnqueueDeletionRel(const String& p_rel) {
        enqueuePackRelativeDeletionRel(m_repair.deletions, p_rel);
    }

    const bool Importer::repairTryAdoptOrphanGltf(const String& p_pack_gltf_name) {
        const String gltfAbsPath        = m_assetPackPath.path_join(p_pack_gltf_name);
        const Dictionary gltfJsonRoot   = getDictionaryFromJsonPath(gltfAbsPath);
        if(gltfJsonRoot.is_empty()) { repairEnqueueDeletionRel(p_pack_gltf_name); return true; }
        Array uriRows;
        appendGltfBuffersAndImagesUriRows(gltfJsonRoot, uriRows);
        Dictionary reservedFilenames;
        collectReservedNamesFromPack(reservedFilenames);
        Dictionary sourcePathToPackName;
        Array sidecarCopyPlans;
        bool anyMissing = false;
        for(int u = 0; u < uriRows.size(); ++u) {
            const Dictionary uriRow       = uriRows[u];
            const String uri              = String(uriRow["uri"]);
            const bool isBuffer           = bool(uriRow["is_buffer"]);
            if(gltfUriCannotMapToLocalFile(uri)) continue;
            const String absoluteFilePath = resolveGltfUriToAbsoluteFile(gltfAbsPath, uri);
            if(absoluteFilePath.is_empty() || !FileAccess::file_exists(absoluteFilePath)) { anyMissing = true; continue; }
            const String simplifiedFilePath = absoluteFilePath.simplify_path();
            if(sourcePathToPackName.has(simplifiedFilePath)) continue;
            const Ref<FileAccess> sidecarFile = FileAccess::open(absoluteFilePath, FileAccess::ModeFlags::READ);
            if(sidecarFile.is_null()) { anyMissing = true; continue; }
            const uint64_t fileSizeBytes  = sidecarFile->get_length();
            const int64_t  fileSizeInt64  = int64_t(fileSizeBytes);
            const char* const sidecarTableName = isBuffer ? BIN_DATA_KEY : TEX_DATA_KEY;
            if(pathIsUnderPack(absoluteFilePath)) {
                const String packFilename = absoluteFilePath.get_file();
                ensureManifestSidecarRow(sidecarTableName, packFilename, packFilename, fileSizeInt64);
                sourcePathToPackName[simplifiedFilePath] = packFilename;
                reservedFilenames[Variant(packFilename)] = true;
                continue;
            }
            const String importRelativePathGuess = absoluteFilePath.get_file();
            String existingPackName;
            if(fileExistsInPackByTruePath(importRelativePathGuess, fileSizeBytes, isBuffer, existingPackName)) {
                sourcePathToPackName[simplifiedFilePath] = existingPackName;
                continue;
            }
            const String newPackFilename = pickPackNameForNewSidecar(absoluteFilePath, isBuffer, fileSizeBytes, reservedFilenames);
            Dictionary sidecarCopyPlan;
            sidecarCopyPlan["source"]    = simplifiedFilePath;
            sidecarCopyPlan["pack_name"] = newPackFilename;
            sidecarCopyPlan["true_path"] = importRelativePathGuess;
            sidecarCopyPlan["is_bin"]    = isBuffer;
            sidecarCopyPlan["size"]      = fileSizeInt64;
            sidecarCopyPlans.append(sidecarCopyPlan);
            reservedFilenames[Variant(newPackFilename)] = true;
            sourcePathToPackName[simplifiedFilePath] = newPackFilename;
        }
        if(anyMissing) { repairEnqueueDeletionRel(p_pack_gltf_name); return true; }
        for(int planIndex = 0; planIndex < sidecarCopyPlans.size(); ++planIndex) {
            const Dictionary sidecarCopyPlan = sidecarCopyPlans[planIndex];
            if(!copySidecarToPackAndRecord(
                    String(sidecarCopyPlan["source"]), String(sidecarCopyPlan["pack_name"]),
                    String(sidecarCopyPlan["true_path"]), bool(sidecarCopyPlan["is_bin"]), int64_t(sidecarCopyPlan["size"]))) {
                repairEnqueueDeletionRel(p_pack_gltf_name);
                return true;
            }
        }
        Dictionary uriToPackName, binSidecarEntries, texSidecarEntries;
        for(int ui = 0; ui < uriRows.size(); ++ui) {
            const Dictionary uriRow       = uriRows[ui];
            const String uri              = String(uriRow["uri"]);
            const String absoluteFilePath = resolveGltfUriToAbsoluteFile(gltfAbsPath, uri);
            if(absoluteFilePath.is_empty()) continue;
            const String simplifiedFilePath = absoluteFilePath.simplify_path();
            if(!sourcePathToPackName.has(simplifiedFilePath)) continue;
            const String assignedPackName = String(sourcePathToPackName[simplifiedFilePath]);
            uriToPackName[uri] = assignedPackName;
            if(bool(uriRow["is_buffer"])) binSidecarEntries[Variant(assignedPackName)] = absoluteFilePath.get_file();
            else                          texSidecarEntries[Variant(assignedPackName)] = absoluteFilePath.get_file();
        }
        String gltfFileContent;
        {
            const Ref<FileAccess> gltfReadFile = FileAccess::open(gltfAbsPath, FileAccess::ModeFlags::READ);
            if(gltfReadFile.is_null()) { repairEnqueueDeletionRel(p_pack_gltf_name); return true; }
            gltfFileContent = gltfReadFile->get_as_text();
        }
        sortDedupAndApplyUriStrings(gltfFileContent, uriToPackName);
        {
            const Ref<FileAccess> gltfWriteFile = FileAccess::open(gltfAbsPath, FileAccess::ModeFlags::WRITE);
            if(gltfWriteFile.is_null()) { repairEnqueueDeletionRel(p_pack_gltf_name); return true; }
            gltfWriteFile->store_string(gltfFileContent);
        }
        {
            const Ref<FileAccess> sizeCheckFile = FileAccess::open(gltfAbsPath, FileAccess::ModeFlags::READ);
            const int64_t orphanFileSizeBytes = sizeCheckFile.is_valid() ? int64_t(sizeCheckFile->get_length()) : 0LL;
            if(orphanFileSizeBytes > 0) {
                const Dictionary assetsDict = manifestAssetsDict();
                const Array assetKeys       = assetsDict.keys();
                for(int ei = 0; ei < assetKeys.size(); ++ei) {
                    const String existingAssetFilename = String(assetKeys[ei]);
                    if(!existingAssetFilename.to_lower().ends_with(".gltf") || existingAssetFilename == p_pack_gltf_name) continue;
                    const Dictionary existingAssetRow = Dictionary(assetsDict[existingAssetFilename]);
                    int64_t existingAssetWeight = existingAssetRow.has(WEIGHT_KEY) ? int64_t(existingAssetRow[WEIGHT_KEY]) : 0LL;
                    if(existingAssetWeight == 0) {
                        const Ref<FileAccess> existingGltfFile = FileAccess::open(m_assetPackPath.path_join(existingAssetFilename), FileAccess::ModeFlags::READ);
                        if(existingGltfFile.is_valid()) existingAssetWeight = int64_t(existingGltfFile->get_length());
                    }
                    if(existingAssetWeight != orphanFileSizeBytes) continue;
                    Dictionary existingBinSidecars, existingTexSidecars;
                    extractBinTexMapsFromAssetRow(existingAssetRow, existingBinSidecars, existingTexSidecars);
                    if(existingBinSidecars.keys().hash() != Dictionary(binSidecarEntries).keys().hash()) continue;
                    if(!fileBinaryEqual(gltfAbsPath, m_assetPackPath.path_join(existingAssetFilename))) continue;
                    repairEnqueueDeletionRel(p_pack_gltf_name);
                    return true;
                }
            }
        }
        const Ref<FileAccess> adoptedGltfFile = FileAccess::open(gltfAbsPath, FileAccess::ModeFlags::READ);
        Dictionary adoptedAssetRow;
        adoptedAssetRow["pack_name"]  = p_pack_gltf_name;
        adoptedAssetRow["true_path"]  = p_pack_gltf_name;
        adoptedAssetRow["group"]      = String("");
        adoptedAssetRow["gltf_size"]  = adoptedGltfFile.is_valid() ? int64_t(adoptedGltfFile->get_length()) : 0LL;
        adoptedAssetRow[BIN_ROW_KEY]  = binSidecarEntries;
        adoptedAssetRow[TEX_ROW_KEY]  = texSidecarEntries;
        recordGltfRowInManifest(adoptedAssetRow);
        return true;
    }

    void Importer::repairFillThumbnailQueueFromManifest() {
        m_repair.thumbnailQueue.clear();
        const Dictionary assetsDict    = manifestAssetsDict();
        const Array assetKeys          = assetsDict.keys();
        const String captureAbsDir     = m_assetPackPath.path_join("capture");
        for(int i = 0; i < assetKeys.size(); ++i) {
            const String assetFilename = String(assetKeys[i]);
            if(!assetFilename.to_lower().ends_with(".gltf") || !assetsDict.has(Variant(assetFilename))) continue;
            const String thumbnailAbsPath = captureAbsDir.path_join(assetFilename.get_basename() + String(".png"));
            if(FileAccess::file_exists(thumbnailAbsPath)) continue;
            const String gltfAbsPath = m_assetPackPath.path_join(assetFilename);
            if(FileAccess::file_exists(gltfAbsPath)) m_repair.thumbnailQueue.append(gltfAbsPath);
        }
    }

    void Importer::repairBuildNonGltfExtensionSizeBuckets() {
        m_repair.sizeBuckets.clear();
        Dictionary filenamesByBucket;
        const auto addSidecarFileToBucket = [&](const String& sidecarFilename) {
            if(sidecarFilename.is_empty() || sidecarFilename.to_lower().ends_with(".gltf")) return;
            const String absoluteFilePath = m_assetPackPath.path_join(sidecarFilename);
            if(!FileAccess::file_exists(absoluteFilePath)) return;
            const Ref<FileAccess> sidecarFile = FileAccess::open(absoluteFilePath, FileAccess::ModeFlags::READ);
            if(sidecarFile.is_null()) return;
            const int64_t fileSizeBytes = int64_t(sidecarFile->get_length());
            if(fileSizeBytes <= 0) return;
            const String bucketKey = sidecarFilename.get_extension().to_lower() + String("|") + String::num_int64(fileSizeBytes);
            dictPushToArray(filenamesByBucket, bucketKey, sidecarFilename);
        };
        const Array binFilenames = manifestBinDict().keys();
        for(int i = 0; i < binFilenames.size(); ++i) addSidecarFileToBucket(String(binFilenames[i]));
        const Array texFilenames = manifestTexDict().keys();
        for(int i = 0; i < texFilenames.size(); ++i) addSidecarFileToBucket(String(texFilenames[i]));
        const Array bucketKeys = filenamesByBucket.keys();
        for(int i = 0; i < bucketKeys.size(); ++i) {
            const Variant bucketNamesVariant = filenamesByBucket[bucketKeys[i]];
            Array candidateFilenames = bucketNamesVariant.get_type() == Variant::Type::ARRAY ? Array(bucketNamesVariant) : Array();
            if(candidateFilenames.size() < 2) continue;
            Dictionary dedupBucket;
            dedupBucket["names"]                  = candidateFilenames;
            dedupBucket[DEDUP_COMPARE_INDEX_KEY] = 1;
            m_repair.sizeBuckets.append(dedupBucket);
        }
    }

    void Importer::repairBuildManifestGltfWeightSideBuckets() {
        m_repair.sizeBuckets.clear();
        if(!m_assetsPackManifest.has(ASSETS_KEY)) return;
        Dictionary assetsDict   = Dictionary(m_assetsPackManifest[ASSETS_KEY]);
        const Array assetKeys   = assetsDict.keys();
        Dictionary assetsByFingerprintKey;
        const auto computeSidecarKeysFingerprint = [](const Dictionary& sidecarDict) {
            Array sortedSidecarKeys = sidecarDict.keys(); sortedSidecarKeys.sort();
            String fingerprint;
            for(int i = 0; i < sortedSidecarKeys.size(); ++i) fingerprint += String(sortedSidecarKeys[i]) + String(";");
            return fingerprint;
        };
        for(int i = 0; i < assetKeys.size(); ++i) {
            const String gltfFilename = String(assetKeys[i]);
            if(!gltfFilename.to_lower().ends_with(".gltf") || !assetsDict.has(Variant(gltfFilename))) continue;
            const Ref<FileAccess> gltfFile = FileAccess::open(m_assetPackPath.path_join(gltfFilename), FileAccess::ModeFlags::READ);
            if(gltfFile.is_null()) continue;
            const int64_t gltfFileSizeBytes = int64_t(gltfFile->get_length());
            if(gltfFileSizeBytes == 0) continue;
            const Dictionary assetRow = Dictionary(assetsDict[gltfFilename]);
            Dictionary binSidecars, texSidecars;
            extractBinTexMapsFromAssetRow(assetRow, binSidecars, texSidecars);
            const String assetFingerprintKey = String::num_int64(gltfFileSizeBytes) + String("::")
                + computeSidecarKeysFingerprint(binSidecars) + String("::") + computeSidecarKeysFingerprint(texSidecars);
            dictPushToArray(assetsByFingerprintKey, assetFingerprintKey, gltfFilename);
        }
        const Array fingerprintKeys = assetsByFingerprintKey.keys();
        for(int j = 0; j < fingerprintKeys.size(); ++j) {
            const Variant candidatesVariant = assetsByFingerprintKey[fingerprintKeys[j]];
            Array candidateFilenames = candidatesVariant.get_type() == Variant::Type::ARRAY ? Array(candidatesVariant) : Array();
            if(candidateFilenames.size() < 2) continue;
            Array assetsWithWeight, assetsWithZeroWeight;
            for(int n = 0; n < candidateFilenames.size(); ++n) {
                const String gltfFilename = String(candidateFilenames[n]);
                const Dictionary assetRow = assetsDict.has(Variant(gltfFilename)) ? Dictionary(assetsDict[Variant(gltfFilename)]) : Dictionary();
                if(assetRow.has(WEIGHT_KEY) && int64_t(assetRow[WEIGHT_KEY]) != 0LL)
                    assetsWithWeight.append(gltfFilename);
                else
                    assetsWithZeroWeight.append(gltfFilename);
            }
            for(int n = 0; n < assetsWithZeroWeight.size(); ++n) assetsWithWeight.append(assetsWithZeroWeight[n]);
            Dictionary dedupBucket;
            dedupBucket["names"]                  = assetsWithWeight;
            dedupBucket[DEDUP_COMPARE_INDEX_KEY] = 1;
            m_repair.sizeBuckets.append(dedupBucket);
        }
    }

    const bool Importer::repairDedupCloneGroupsOneStep() {
        while(true) {
            if(m_repair.dedupIdx >= m_repair.sizeBuckets.size()) return false;
            Dictionary currentBucket = Dictionary(m_repair.sizeBuckets[int(m_repair.dedupIdx)]);
            const Variant namesVariant = currentBucket["names"];
            if(namesVariant.get_type() != Variant::Type::ARRAY) { ++m_repair.dedupIdx; continue; }
            Array bucketFilenames = Array(namesVariant);
            if(bucketFilenames.size() < 2) {
                if(!m_repair.dedupIsGltfWave) { currentBucket.erase(DEDUP_COMPARE_INDEX_KEY); m_repair.sizeBuckets[int(m_repair.dedupIdx)] = currentBucket; }
                ++m_repair.dedupIdx;
                continue;
            }
            int64_t compareIndex = currentBucket.has(DEDUP_COMPARE_INDEX_KEY) ? int64_t(currentBucket[DEDUP_COMPARE_INDEX_KEY]) : 1LL;
            if(compareIndex <= 0) compareIndex = 1LL;
            if(compareIndex >= int64_t(bucketFilenames.size())) {
                bucketFilenames.remove_at(0);
                compareIndex = 1LL;
                repairDedupPersistBucket(currentBucket, bucketFilenames, compareIndex);
                if(bucketFilenames.size() < 2) ++m_repair.dedupIdx;
                return true;
            }
            const String canonicalFilename  = String(bucketFilenames[0]);
            const String candidateFilename  = String(bucketFilenames[int(compareIndex)]);
            if(m_repair.dedupIsGltfWave) {
                const uint64_t compareStartUsec = Time::get_singleton()->get_ticks_usec();
                if(!canonicalFilename.to_lower().ends_with(".gltf") || !candidateFilename.to_lower().ends_with(".gltf")) {
                    ++compareIndex; repairDedupPersistJjOnly(currentBucket, compareIndex); debitTimeBudgetFromTicks(compareStartUsec); return true;
                }
                const Dictionary assetsDict = manifestAssetsDict();
                if(!assetsDict.has(Variant(canonicalFilename)) || !assetsDict.has(Variant(candidateFilename))) {
                    ++compareIndex; repairDedupPersistJjOnly(currentBucket, compareIndex); debitTimeBudgetFromTicks(compareStartUsec); return true;
                }
                const bool filesAreIdentical = fileBinaryEqual(m_assetPackPath.path_join(canonicalFilename), m_assetPackPath.path_join(candidateFilename));
                debitTimeBudgetFromTicks(compareStartUsec);
                if(!filesAreIdentical) { ++compareIndex; repairDedupPersistJjOnly(currentBucket, compareIndex); return true; }
                const uint64_t mergeStartUsec = Time::get_singleton()->get_ticks_usec();
                mergeRepairDuplicateGltfPackFiles(canonicalFilename, candidateFilename, true, true);
                debitTimeBudgetFromTicks(mergeStartUsec);
                bucketFilenames.remove_at(int(compareIndex));
                compareIndex = compareIndex < int64_t(bucketFilenames.size()) ? compareIndex : 1LL;
                if(compareIndex <= 0) compareIndex = 1LL;
                repairDedupPersistBucket(currentBucket, bucketFilenames, compareIndex);
                return true;
            }
            const Dictionary binTable = manifestBinDict();
            const Dictionary texTable = manifestTexDict();
            const uint64_t compareStartUsec = Time::get_singleton()->get_ticks_usec();
            bool filesAreIdentical = false;
            if(binTable.has(Variant(canonicalFilename)) && binTable.has(Variant(candidateFilename)))
                filesAreIdentical = fileBinaryEqual(m_assetPackPath.path_join(canonicalFilename), m_assetPackPath.path_join(candidateFilename));
            else if(texTable.has(Variant(canonicalFilename)) && texTable.has(Variant(candidateFilename)))
                filesAreIdentical = fileBinaryEqual(m_assetPackPath.path_join(canonicalFilename), m_assetPackPath.path_join(candidateFilename));
            else { ++compareIndex; repairDedupPersistJjOnly(currentBucket, compareIndex); debitTimeBudgetFromTicks(compareStartUsec); return true; }
            debitTimeBudgetFromTicks(compareStartUsec);
            if(!filesAreIdentical) { ++compareIndex; repairDedupPersistJjOnly(currentBucket, compareIndex); return true; }
            const char* const sidecarTableName = candidateFilename.to_lower().ends_with(".bin") ? BIN_DATA_KEY : TEX_DATA_KEY;
            const uint64_t mergeStartUsec = Time::get_singleton()->get_ticks_usec();
            mergeRepairPackSidecarsInTable(sidecarTableName, canonicalFilename, candidateFilename, true);
            debitTimeBudgetFromTicks(mergeStartUsec);
            bucketFilenames.remove_at(int(compareIndex));
            compareIndex = compareIndex < int64_t(bucketFilenames.size()) ? compareIndex : 1LL;
            if(compareIndex <= 0) compareIndex = 1LL;
            repairDedupPersistBucket(currentBucket, bucketFilenames, compareIndex);
            return true;
        }
    }

    void Importer::repairDedupPersistJjOnly(Dictionary& p_bucket, int64_t p_compare_index) {
        p_bucket[DEDUP_COMPARE_INDEX_KEY] = p_compare_index;
        m_repair.sizeBuckets[int(m_repair.dedupIdx)] = p_bucket;
    }

    void Importer::repairDedupPersistBucket(Dictionary& p_bucket, const Array& p_names, int64_t p_compare_index) {
        p_bucket["names"]                  = p_names;
        p_bucket[DEDUP_COMPARE_INDEX_KEY] = p_compare_index;
        m_repair.sizeBuckets[int(m_repair.dedupIdx)] = p_bucket;
    }

    void Importer::repairCollectDeletionCandidates() {
        pruneUnreferencedSidecarsAndEnqueue(m_repair.deletions);
        Dictionary referencedFilenames;
        buildAllowedReferencedPackFilenames(referencedFilenames);
        const Dictionary assetsDict = manifestAssetsDict();
        const String captureSubdir  = String("capture");
        const String captureAbsDir  = m_assetPackPath.path_join(captureSubdir);
        Array captureDirectoryFilenames;
        enumerateFilesInDirNonRecursive(captureAbsDir, captureDirectoryFilenames);
        for(int i = 0; i < captureDirectoryFilenames.size(); ++i) {
            const String captureFilename = String(captureDirectoryFilenames[i]);
            if(captureFilename.to_lower().ends_with(".png")) {
                const String expectedGltfFilename = captureFilename.get_basename() + String(".gltf");
                if(!assetsDict.has(Variant(expectedGltfFilename)))
                    repairEnqueueDeletionRel(captureSubdir.path_join(captureFilename));
            }
        }
        Array packRootFilenames;
        enumerateFilesInDirNonRecursive(m_assetPackPath, packRootFilenames);
        for(int i = 0; i < packRootFilenames.size(); ++i) {
            const String rootFilename      = String(packRootFilenames[i]);
            const String rootFilenameLower = rootFilename.to_lower();
            const bool hasKnownExtension   = rootFilenameLower.ends_with(".gltf") || rootFilenameLower.ends_with(".bin")
                || rootFilenameLower.ends_with(".png") || rootFilenameLower.ends_with(".jpg") || rootFilenameLower.ends_with(".jpeg");
            if(hasKnownExtension && !referencedFilenames.has(Variant(rootFilename))) repairEnqueueDeletionRel(rootFilename);
        }
    }

    void Importer::repairBeginThumbnailGeneration() {
        repairFillThumbnailQueueFromManifest();
        if(!m_repair.thumbnailQueue.is_empty()) {
            m_picturing.toCapture     = m_repair.thumbnailQueue;
            m_repair.thumbnailQueue.clear();
            m_picturing.followsRepair = true;
            m_picturing.returnPhase   = E_RepairSubPhase::FinalizeRepairWrite;
            m_importerState           = E_ImporterState::Picturing;
        } else {
            repairFinalizeAfterRepairWrite();
            m_repair.subPhase = E_RepairSubPhase::SubDone;
        }
    }

    void Importer::repairFinalizeAfterRepairWrite() {
        runFullManifestSidecarAndGltfDedupCommit(true);
    }

    void Importer::mergeRepairPackSidecarsInTable(const String& p_table, const String& p_keep,
        const String& p_drop, bool p_defer_delete) {
        if(!m_assetsPackManifest.has(p_table)) return;
        Dictionary sidecarTable = Dictionary(m_assetsPackManifest[p_table]);
        if(!sidecarTable.has(Variant(p_keep)) || !sidecarTable.has(Variant(p_drop))) return;
        if(m_assetsPackManifest.has(ASSETS_KEY)) {
            Dictionary assetsDict  = Dictionary(m_assetsPackManifest[ASSETS_KEY]);
            const Array assetKeys  = assetsDict.keys();
            for(int ax = 0; ax < assetKeys.size(); ++ax) {
                const String gltfFilename = String(assetKeys[ax]);
                if(!assetsDict.has(Variant(gltfFilename))) continue;
                const Dictionary assetRow = Dictionary(assetsDict[gltfFilename]);
                Dictionary binSidecars, texSidecars;
                extractBinTexMapsFromAssetRow(assetRow, binSidecars, texSidecars);
                bool assetWasModified = false;
                if(binSidecars.has(Variant(p_drop)) && p_table == String(BIN_DATA_KEY)) {
                    const Variant droppedBinRow = binSidecars[p_drop];
                    binSidecars.erase(Variant(p_drop));
                    if(!binSidecars.has(Variant(p_keep))) binSidecars[Variant(p_keep)] = droppedBinRow;
                    assetWasModified = true;
                }
                if(texSidecars.has(Variant(p_drop)) && p_table == String(TEX_DATA_KEY)) {
                    const Variant droppedTexRow = texSidecars[p_drop];
                    texSidecars.erase(Variant(p_drop));
                    if(!texSidecars.has(Variant(p_keep))) texSidecars[Variant(p_keep)] = droppedTexRow;
                    assetWasModified = true;
                }
                if(assetWasModified) {
                    Dictionary updatedAssetRow = assetRow;
                    updatedAssetRow[BIN_ROW_KEY] = binSidecars;
                    updatedAssetRow[TEX_ROW_KEY] = texSidecars;
                    tryReplaceJsonQuotedStringInFile(m_assetPackPath.path_join(gltfFilename), p_drop, p_keep);
                    const Ref<FileAccess> gltfFile = FileAccess::open(m_assetPackPath.path_join(gltfFilename), FileAccess::ModeFlags::READ);
                    if(gltfFile.is_valid()) updatedAssetRow[WEIGHT_KEY] = int64_t(gltfFile->get_length());
                    assetsDict[Variant(gltfFilename)] = updatedAssetRow;
                }
            }
            m_assetsPackManifest[ASSETS_KEY] = assetsDict;
        }
        sidecarTable.erase(Variant(p_drop));
        m_assetsPackManifest[p_table] = sidecarTable;
        if(p_defer_delete) repairEnqueueDeletionRel(p_drop);
        else               deletePackRelativeFile(p_drop);
    }

    const bool Importer::mergeRepairDuplicateGltfPackFiles(const String& p_keep, const String& p_drop,
        bool p_defer_delete, bool p_already_equal) {
        if(!m_assetsPackManifest.has(ASSETS_KEY)) return false;
        Dictionary assetsDict = Dictionary(m_assetsPackManifest[ASSETS_KEY]);
        if(!assetsDict.has(Variant(p_keep)) || !assetsDict.has(Variant(p_drop))) return false;
        if(!p_already_equal && !fileBinaryEqual(m_assetPackPath.path_join(p_keep), m_assetPackPath.path_join(p_drop)))
            return false;
        assetsDict.erase(Variant(p_drop));
        m_assetsPackManifest[ASSETS_KEY] = assetsDict;
        const String droppedThumbnailPath = packRelativeCapturePngForGltfPackName(p_drop);
        if(p_defer_delete) {
            repairEnqueueDeletionRel(p_drop);
            repairEnqueueDeletionRel(droppedThumbnailPath);
        } else {
            deletePackRelativeFile(p_drop);
            deletePackRelativeFile(droppedThumbnailPath);
        }
        return true;
    }

    void Importer::listPackRootGltfFileNames(Array& p_out_base_names) {
        p_out_base_names.clear();
        if(m_assetPackPath.is_empty()) return;
        Array allPackRootFilenames;
        enumerateFilesInDirNonRecursive(m_assetPackPath, allPackRootFilenames);
        for(int i = 0; i < allPackRootFilenames.size(); ++i) {
            const String filename = String(allPackRootFilenames[i]);
            if(filename.to_lower().ends_with(".gltf")) p_out_base_names.append(filename);
        }
    }

}
