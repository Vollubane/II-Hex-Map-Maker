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
        if(m_importerPictureMakerScene.is_null()) return;
        while(m_picturing.idle.size() + m_picturing.active.size() < PICTURE_MAKER_MAX && 0.0 < m_timeBudget) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            Node* n = m_importerPictureMakerScene->instantiate();
            if(!n) { UtilityFunctions::push_error("Importer: picturing, instantiate ImporterPictureMaker failed"); break; }
            ImporterPictureMaker* ipm = Object::cast_to<ImporterPictureMaker>(n);
            if(!ipm) {
                UtilityFunctions::push_error(String("Importer: picturing, scene root is not ImporterPictureMaker: ") + n->get_class());
                n->queue_free(); debitTimeBudgetFromTicks(t0); break;
            }
            add_child(ipm);
            m_picturing.idle.append(ipm);
            debitTimeBudgetFromTicks(t0);
        }
        for(int i = m_picturing.active.size() - 1; 0 <= i && 0.0 < m_timeBudget; --i) {
            Object* o = m_picturing.active[i].operator Object*();
            if(!o) {
                UtilityFunctions::push_error(String("Importer: picturing, null in active at index ") + String::num_int64(i));
                m_picturing.active.remove_at(i); continue;
            }
            ImporterPictureMaker* w = Object::cast_to<ImporterPictureMaker>(o);
            if(!w) {
                UtilityFunctions::push_error(String("Importer: picturing, active[") + String::num_int64(i)
                    + String("] is not ImporterPictureMaker: ") + o->get_class());
                if(Node* bad = Object::cast_to<Node>(o)) bad->queue_free();
                m_picturing.active.remove_at(i); continue;
            }
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const ImporterPictureMaker::E_ImporterPictureMakerState s = w->stepProgress();
            debitTimeBudgetFromTicks(t0);
            if(s == ImporterPictureMaker::E_ImporterPictureMakerState::Waiting) {
                m_picturing.active.remove_at(i);
                m_picturing.idle.append(w);
            }
        }
        while(0.0 < m_timeBudget && 0 < m_picturing.toCapture.size() && 0 < m_picturing.idle.size()) {
            const uint64_t t0  = Time::get_singleton()->get_ticks_usec();
            const int lasti    = m_picturing.idle.size() - 1;
            Object* o = m_picturing.idle[lasti].operator Object*();
            if(!o) {
                UtilityFunctions::push_error(String("Importer: picturing, null in idle at index ") + String::num_int64(lasti));
                m_picturing.idle.remove_at(lasti); debitTimeBudgetFromTicks(t0); continue;
            }
            ImporterPictureMaker* ipm = Object::cast_to<ImporterPictureMaker>(o);
            if(!ipm) {
                UtilityFunctions::push_error(String("Importer: picturing, idle[") + String::num_int64(lasti)
                    + String("] is not ImporterPictureMaker: ") + o->get_class());
                if(Node* bad = Object::cast_to<Node>(o)) bad->queue_free();
                m_picturing.idle.remove_at(lasti); debitTimeBudgetFromTicks(t0); continue;
            }
            m_picturing.idle.remove_at(lasti);
            const String path = m_picturing.toCapture[0];
            m_picturing.toCapture.remove_at(0);
            ipm->makeAPicture(path);
            m_picturing.active.append(ipm);
            debitTimeBudgetFromTicks(t0);
        }
    }

    void Importer::runRemovingAssetsSlice() {
        while(0.0 < m_timeBudget && m_remove.subPhase != E_RemoveSubPhase::SubDone) {
            switch(m_remove.subPhase) {
                case E_RemoveSubPhase::ListedAssetsAndDrainDeletions: {
                    if(!m_remove.deletions.queue.is_empty()) {
                        drainPackRelativeDeletionQueueSlice(m_remove.deletions);
                        break;
                    }
                    if(!m_remove.gltfKeys.is_empty()) {
                        const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
                        const String key  = String(m_remove.gltfKeys[0]);
                        m_remove.gltfKeys.remove_at(0);
                        Dictionary assets = manifestAssetsDict();
                        if(assets.has(Variant(key))) {
                            assets.erase(Variant(key));
                            m_assetsPackManifest[ASSETS_KEY] = assets;
                        }
                        removeEnqueueDeletionRel(key);
                        removeEnqueueDeletionRel(packRelativeCapturePngForGltfPackName(key));
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
        const Dictionary A = manifestAssetsDict();
        const Array ak = A.keys();
        for(int ai = 0; ai < ak.size(); ++ai) {
            if(!A.has(ak[ai])) continue;
            Dictionary bd, td;
            extractBinTexMapsFromAssetRow(Dictionary(A[ak[ai]]), bd, td);
            const Array bk = bd.keys();
            for(int bi = 0; bi < bk.size(); ++bi) {
                const String pk = String("b/") + String(bk[bi]);
                refCounts[pk] = dictGetInt(refCounts, pk) + 1;
            }
            const Array tk = td.keys();
            for(int ti = 0; ti < tk.size(); ++ti) {
                const String pk = String("t/") + String(tk[ti]);
                refCounts[pk] = dictGetInt(refCounts, pk) + 1;
            }
        }
        for(int pass = 0; pass < 2; ++pass) {
            const char* const tname = pass == 0 ? BIN_DATA_KEY : TEX_DATA_KEY;
            if(!m_assetsPackManifest.has(tname)) continue;
            Dictionary T = Dictionary(m_assetsPackManifest[tname]);
            const Array keys = T.keys();
            for(int i = 0; i < keys.size(); ++i) {
                const String pk   = String(keys[i]);
                if(!T.has(Variant(pk))) continue;
                const String pref = (pass == 0 ? String("b/") : String("t/")) + pk;
                if(dictGetInt(refCounts, pref) == 0LL) {
                    T.erase(Variant(pk));
                    enqueuePackRelativeDeletionRel(p_dq, pk);
                }
            }
            m_assetsPackManifest[tname] = T;
        }
    }

    void Importer::removeEnqueueDeletionRel(const String& p_rel) {
        enqueuePackRelativeDeletionRel(m_remove.deletions, p_rel);
    }

    void Importer::runRepairPackSlice() {
        while(0.0 < m_timeBudget) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            bool elapsedCharged = false;
            switch(m_repair.subPhase) {
                case E_RepairSubPhase::BuildOrphanGltfList: {
                    m_repair.workQueue.clear();
                    Array diskGltf;
                    listPackRootGltfFileNames(diskGltf);
                    const Dictionary ad = manifestAssetsDict();
                    for(int i = 0; i < diskGltf.size(); ++i) {
                        const String nm = String(diskGltf[i]);
                        if(nm.is_empty() || nm == "." || nm == "..") continue;
                        if(!ad.has(Variant(nm))) m_repair.workQueue.append(nm);
                    }
                    m_repair.subPhase = E_RepairSubPhase::ResolveOrphanGltfSlice;
                } break;

                case E_RepairSubPhase::ResolveOrphanGltfSlice: {
                    if(0.0 < m_timeBudget && !m_repair.workQueue.is_empty()) {
                        const String one = String(m_repair.workQueue[0]);
                        m_repair.workQueue.remove_at(0);
                        repairTryAdoptOrphanGltf(one);
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
        const String fullGltf      = m_assetPackPath.path_join(p_pack_gltf_name);
        const Dictionary maybeRoot = getDictionaryFromJsonPath(fullGltf);
        if(maybeRoot.is_empty()) { repairEnqueueDeletionRel(p_pack_gltf_name); return true; }
        Array ulist;
        appendGltfBuffersAndImagesUriRows(maybeRoot, ulist);
        Dictionary reserved;
        collectReservedNamesFromPack(reserved);
        Dictionary srcToPack;
        Array sidecarCopyPlans;
        bool anyMissing = false;
        for(int u = 0; u < ulist.size(); ++u) {
            const Dictionary um  = ulist[u];
            const String uri     = String(um["uri"]);
            const bool isBuf     = bool(um["is_buffer"]);
            if(gltfUriCannotMapToLocalFile(uri)) continue;
            const String absFile = resolveGltfUriToAbsoluteFile(fullGltf, uri);
            if(absFile.is_empty() || !FileAccess::file_exists(absFile)) { anyMissing = true; continue; }
            const String simp = absFile.simplify_path();
            if(srcToPack.has(simp)) continue;
            const Ref<FileAccess> rf = FileAccess::open(absFile, FileAccess::ModeFlags::READ);
            if(rf.is_null()) { anyMissing = true; continue; }
            const uint64_t szBytes = rf->get_length();
            const int64_t  isize   = int64_t(szBytes);
            const char* const tbl  = isBuf ? BIN_DATA_KEY : TEX_DATA_KEY;
            if(pathIsUnderPack(absFile)) {
                const String pfn = absFile.get_file();
                ensureManifestSidecarRow(tbl, pfn, pfn, isize);
                srcToPack[simp] = pfn;
                reserved[Variant(pfn)] = true;
                continue;
            }
            const String tRelGuess = absFile.get_file();
            String reusePackName;
            if(fileExistsInPackByTruePath(tRelGuess, szBytes, isBuf, reusePackName)) {
                srcToPack[simp] = reusePackName;
                continue;
            }
            const String newPackFn = pickPackNameForNewSidecar(absFile, isBuf, szBytes, reserved);
            Dictionary plan;
            plan["source"]    = simp;
            plan["pack_name"] = newPackFn;
            plan["true_path"] = tRelGuess;
            plan["is_bin"]    = isBuf;
            plan["size"]      = isize;
            sidecarCopyPlans.append(plan);
            reserved[Variant(newPackFn)] = true;
            srcToPack[simp] = newPackFn;
        }
        if(anyMissing) { repairEnqueueDeletionRel(p_pack_gltf_name); return true; }
        for(int cpi = 0; cpi < sidecarCopyPlans.size(); ++cpi) {
            const Dictionary dcp = sidecarCopyPlans[cpi];
            if(!copySidecarToPackAndRecord(
                    String(dcp["source"]), String(dcp["pack_name"]),
                    String(dcp["true_path"]), bool(dcp["is_bin"]), int64_t(dcp["size"]))) {
                repairEnqueueDeletionRel(p_pack_gltf_name);
                return true;
            }
        }
        Dictionary uriMap, binD, texD;
        for(int ii = 0; ii < ulist.size(); ++ii) {
            const Dictionary wm  = ulist[ii];
            const String ur      = String(wm["uri"]);
            const String absR    = resolveGltfUriToAbsoluteFile(fullGltf, ur);
            if(absR.is_empty()) continue;
            const String simplified = absR.simplify_path();
            if(!srcToPack.has(simplified)) continue;
            const String newName = String(srcToPack[simplified]);
            uriMap[ur] = newName;
            if(bool(wm["is_buffer"])) binD[Variant(newName)] = absR.get_file();
            else                      texD[Variant(newName)] = absR.get_file();
        }
        String gltfTxt;
        {
            const Ref<FileAccess> rgf = FileAccess::open(fullGltf, FileAccess::ModeFlags::READ);
            if(rgf.is_null()) { repairEnqueueDeletionRel(p_pack_gltf_name); return true; }
            gltfTxt = rgf->get_as_text();
        }
        sortDedupAndApplyUriStrings(gltfTxt, uriMap);
        {
            const Ref<FileAccess> wgf = FileAccess::open(fullGltf, FileAccess::ModeFlags::WRITE);
            if(wgf.is_null()) { repairEnqueueDeletionRel(p_pack_gltf_name); return true; }
            wgf->store_string(gltfTxt);
        }
        {
            const Ref<FileAccess> sizeRf = FileAccess::open(fullGltf, FileAccess::ModeFlags::READ);
            const int64_t orphanSz = sizeRf.is_valid() ? int64_t(sizeRf->get_length()) : 0LL;
            if(orphanSz > 0) {
                const Dictionary A = manifestAssetsDict();
                const Array ak     = A.keys();
                for(int ek = 0; ek < ak.size(); ++ek) {
                    const String exKey = String(ak[ek]);
                    if(!exKey.to_lower().ends_with(".gltf") || exKey == p_pack_gltf_name) continue;
                    const Dictionary exRow = Dictionary(A[exKey]);
                    int64_t exW = exRow.has(WEIGHT_KEY) ? int64_t(exRow[WEIGHT_KEY]) : 0LL;
                    if(exW == 0) {
                        const Ref<FileAccess> exRf = FileAccess::open(m_assetPackPath.path_join(exKey), FileAccess::ModeFlags::READ);
                        if(exRf.is_valid()) exW = int64_t(exRf->get_length());
                    }
                    if(exW != orphanSz) continue;
                    Dictionary exBd, exTd;
                    extractBinTexMapsFromAssetRow(exRow, exBd, exTd);
                    if(exBd.keys().hash() != Dictionary(binD).keys().hash()) continue;
                    if(!fileBinaryEqual(fullGltf, m_assetPackPath.path_join(exKey))) continue;
                    repairEnqueueDeletionRel(p_pack_gltf_name);
                    return true;
                }
            }
        }
        const Ref<FileAccess> glenRf = FileAccess::open(fullGltf, FileAccess::ModeFlags::READ);
        Dictionary itemAdopt;
        itemAdopt["pack_name"]  = p_pack_gltf_name;
        itemAdopt["true_path"]  = p_pack_gltf_name;
        itemAdopt["group"]      = String("");
        itemAdopt["gltf_size"]  = glenRf.is_valid() ? int64_t(glenRf->get_length()) : 0LL;
        itemAdopt[BIN_ROW_KEY]  = binD;
        itemAdopt[TEX_ROW_KEY]  = texD;
        recordGltfRowInManifest(itemAdopt);
        return true;
    }

    void Importer::repairFillThumbnailQueueFromManifest() {
        m_repair.thumbnailQueue.clear();
        const Dictionary A         = manifestAssetsDict();
        const Array aks            = A.keys();
        const String captureAbsDir = m_assetPackPath.path_join("capture");
        for(int i = 0; i < aks.size(); ++i) {
            const String gname = String(aks[i]);
            if(!gname.to_lower().ends_with(".gltf") || !A.has(Variant(gname))) continue;
            const String pngAbs = captureAbsDir.path_join(gname.get_basename() + String(".png"));
            if(FileAccess::file_exists(pngAbs)) continue;
            const String gpAbs = m_assetPackPath.path_join(gname);
            if(FileAccess::file_exists(gpAbs)) m_repair.thumbnailQueue.append(gpAbs);
        }
    }

    void Importer::repairBuildNonGltfExtensionSizeBuckets() {
        m_repair.sizeBuckets.clear();
        Dictionary groupNames;
        const auto add_fname = [&](const String& fname) {
            if(fname.is_empty() || fname.to_lower().ends_with(".gltf")) return;
            const String full = m_assetPackPath.path_join(fname);
            if(!FileAccess::file_exists(full)) return;
            const Ref<FileAccess> rf = FileAccess::open(full, FileAccess::ModeFlags::READ);
            if(rf.is_null()) return;
            const int64_t len = int64_t(rf->get_length());
            if(len <= 0) return;
            const String grp = fname.get_extension().to_lower() + String("|") + String::num_int64(len);
            dictPushToArray(groupNames, grp, fname);
        };
        const Array bk = manifestBinDict().keys();
        for(int i = 0; i < bk.size(); ++i) add_fname(String(bk[i]));
        const Array tk = manifestTexDict().keys();
        for(int i = 0; i < tk.size(); ++i) add_fname(String(tk[i]));
        const Array gkk = groupNames.keys();
        for(int i = 0; i < gkk.size(); ++i) {
            const Variant vgn = groupNames[gkk[i]];
            Array names = vgn.get_type() == Variant::Type::ARRAY ? Array(vgn) : Array();
            if(names.size() < 2) continue;
            Dictionary bk2;
            bk2["names"] = names;
            bk2["_j"]    = 1;
            m_repair.sizeBuckets.append(bk2);
        }
    }

    void Importer::repairBuildManifestGltfWeightSideBuckets() {
        m_repair.sizeBuckets.clear();
        if(!m_assetsPackManifest.has(ASSETS_KEY)) return;
        Dictionary A        = Dictionary(m_assetsPackManifest[ASSETS_KEY]);
        const Array ak      = A.keys();
        Dictionary groupKeys;
        const auto keysFingerprint = [](const Dictionary& d) {
            Array kk = d.keys(); kk.sort();
            String acc;
            for(int i = 0; i < kk.size(); ++i) acc += String(kk[i]) + String(";");
            return acc;
        };
        for(int i = 0; i < ak.size(); ++i) {
            const String gnm = String(ak[i]);
            if(!gnm.to_lower().ends_with(".gltf") || !A.has(Variant(gnm))) continue;
            const Ref<FileAccess> szRf = FileAccess::open(m_assetPackPath.path_join(gnm), FileAccess::ModeFlags::READ);
            if(szRf.is_null()) continue;
            const int64_t w = int64_t(szRf->get_length());
            if(w == 0) continue;
            const Dictionary rd = Dictionary(A[gnm]);
            Dictionary bd, td;
            extractBinTexMapsFromAssetRow(rd, bd, td);
            const String fk = String::num_int64(w) + String("::")
                + keysFingerprint(bd) + String("::") + keysFingerprint(td);
            dictPushToArray(groupKeys, fk, gnm);
        }
        const Array gkz = groupKeys.keys();
        for(int j = 0; j < gkz.size(); ++j) {
            const Variant vw = groupKeys[gkz[j]];
            Array nmz = vw.get_type() == Variant::Type::ARRAY ? Array(vw) : Array();
            if(nmz.size() < 2) continue;
            Array ordered, zeros;
            for(int n = 0; n < nmz.size(); ++n) {
                const String nm  = String(nmz[n]);
                const Dictionary row = A.has(Variant(nm)) ? Dictionary(A[Variant(nm)]) : Dictionary();
                if(row.has(WEIGHT_KEY) && int64_t(row[WEIGHT_KEY]) != 0LL)
                    ordered.append(nm);
                else
                    zeros.append(nm);
            }
            for(int n = 0; n < zeros.size(); ++n) ordered.append(zeros[n]);
            Dictionary bkq;
            bkq["names"] = ordered;
            bkq["_j"]    = 1;
            m_repair.sizeBuckets.append(bkq);
        }
    }

    const bool Importer::repairDedupCloneGroupsOneStep() {
        while(true) {
            if(m_repair.dedupIdx >= m_repair.sizeBuckets.size()) return false;
            Dictionary bucket = Dictionary(m_repair.sizeBuckets[int(m_repair.dedupIdx)]);
            const Variant vn  = bucket["names"];
            if(vn.get_type() != Variant::Type::ARRAY) { ++m_repair.dedupIdx; continue; }
            Array names = Array(vn);
            if(names.size() < 2) {
                if(!m_repair.dedupIsGltfWave) { bucket.erase("_j"); m_repair.sizeBuckets[int(m_repair.dedupIdx)] = bucket; }
                ++m_repair.dedupIdx;
                continue;
            }
            int64_t jj = bucket.has("_j") ? int64_t(bucket["_j"]) : 1LL;
            if(jj <= 0) jj = 1LL;
            if(jj >= int64_t(names.size())) {
                names.remove_at(0);
                jj = 1LL;
                repairDedupPersistBucket(bucket, names, jj);
                if(names.size() < 2) ++m_repair.dedupIdx;
                return true;
            }
            const String first = String(names[0]);
            const String cand  = String(names[int(jj)]);
            if(m_repair.dedupIsGltfWave) {
                const uint64_t t_cmp = Time::get_singleton()->get_ticks_usec();
                if(!first.to_lower().ends_with(".gltf") || !cand.to_lower().ends_with(".gltf")) {
                    ++jj; repairDedupPersistJjOnly(bucket, jj); debitTimeBudgetFromTicks(t_cmp); return true;
                }
                const Dictionary A_assets = manifestAssetsDict();
                if(!A_assets.has(Variant(first)) || !A_assets.has(Variant(cand))) {
                    ++jj; repairDedupPersistJjOnly(bucket, jj); debitTimeBudgetFromTicks(t_cmp); return true;
                }
                const bool sameG = fileBinaryEqual(m_assetPackPath.path_join(first), m_assetPackPath.path_join(cand));
                debitTimeBudgetFromTicks(t_cmp);
                if(!sameG) { ++jj; repairDedupPersistJjOnly(bucket, jj); return true; }
                const uint64_t t_merge = Time::get_singleton()->get_ticks_usec();
                mergeRepairDuplicateGltfPackFiles(first, cand, true, true);
                debitTimeBudgetFromTicks(t_merge);
                names.remove_at(int(jj));
                jj = jj < int64_t(names.size()) ? jj : 1LL;
                if(jj <= 0) jj = 1LL;
                repairDedupPersistBucket(bucket, names, jj);
                return true;
            }
            const Dictionary binG = manifestBinDict();
            const Dictionary texG = manifestTexDict();
            const uint64_t t_cmp  = Time::get_singleton()->get_ticks_usec();
            bool sameBytes = false;
            if(binG.has(Variant(first)) && binG.has(Variant(cand)))
                sameBytes = fileBinaryEqual(m_assetPackPath.path_join(first), m_assetPackPath.path_join(cand));
            else if(texG.has(Variant(first)) && texG.has(Variant(cand)))
                sameBytes = fileBinaryEqual(m_assetPackPath.path_join(first), m_assetPackPath.path_join(cand));
            else { ++jj; repairDedupPersistJjOnly(bucket, jj); debitTimeBudgetFromTicks(t_cmp); return true; }
            debitTimeBudgetFromTicks(t_cmp);
            if(!sameBytes) { ++jj; repairDedupPersistJjOnly(bucket, jj); return true; }
            const char* const tbl  = cand.to_lower().ends_with(".bin") ? BIN_DATA_KEY : TEX_DATA_KEY;
            const uint64_t t_merge = Time::get_singleton()->get_ticks_usec();
            mergeRepairPackSidecarsInTable(tbl, first, cand, true);
            debitTimeBudgetFromTicks(t_merge);
            names.remove_at(int(jj));
            jj = jj < int64_t(names.size()) ? jj : 1LL;
            if(jj <= 0) jj = 1LL;
            repairDedupPersistBucket(bucket, names, jj);
            return true;
        }
    }

    void Importer::repairDedupPersistJjOnly(Dictionary& p_bucket, int64_t p_jj) {
        p_bucket["_j"] = p_jj;
        m_repair.sizeBuckets[int(m_repair.dedupIdx)] = p_bucket;
    }

    void Importer::repairDedupPersistBucket(Dictionary& p_bucket, const Array& p_names, int64_t p_jj) {
        p_bucket["names"] = p_names;
        p_bucket["_j"]    = p_jj;
        m_repair.sizeBuckets[int(m_repair.dedupIdx)] = p_bucket;
    }

    void Importer::repairCollectDeletionCandidates() {
        pruneUnreferencedSidecarsAndEnqueue(m_repair.deletions);
        Dictionary allowed;
        buildAllowedReferencedPackFilenames(allowed);
        const Dictionary A_assets = manifestAssetsDict();
        const String capRel       = String("capture");
        const String capAbs       = m_assetPackPath.path_join(capRel);
        Array captureFiles;
        enumerateFilesInDirNonRecursive(capAbs, captureFiles);
        for(int i = 0; i < captureFiles.size(); ++i) {
            const String q = String(captureFiles[i]);
            if(q.to_lower().ends_with(".png")) {
                const String gltfKey = q.get_basename() + String(".gltf");
                if(!A_assets.has(Variant(gltfKey)))
                    repairEnqueueDeletionRel(capRel.path_join(q));
            }
        }
        Array rootFiles;
        enumerateFilesInDirNonRecursive(m_assetPackPath, rootFiles);
        for(int i = 0; i < rootFiles.size(); ++i) {
            const String rn  = String(rootFiles[i]);
            const String rnl = rn.to_lower();
            const bool extOk = rnl.ends_with(".gltf") || rnl.ends_with(".bin")
                || rnl.ends_with(".png") || rnl.ends_with(".jpg") || rnl.ends_with(".jpeg");
            if(extOk && !allowed.has(Variant(rn))) repairEnqueueDeletionRel(rn);
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
        Dictionary ttbl = Dictionary(m_assetsPackManifest[p_table]);
        if(!ttbl.has(Variant(p_keep)) || !ttbl.has(Variant(p_drop))) return;
        if(m_assetsPackManifest.has(ASSETS_KEY)) {
            Dictionary A   = Dictionary(m_assetsPackManifest[ASSETS_KEY]);
            const Array ak = A.keys();
            for(int ax = 0; ax < ak.size(); ++ax) {
                const String gnm = String(ak[ax]);
                if(!A.has(Variant(gnm))) continue;
                const Dictionary gdata = Dictionary(A[gnm]);
                Dictionary bd, td;
                extractBinTexMapsFromAssetRow(gdata, bd, td);
                bool mut = false;
                if(bd.has(Variant(p_drop)) && p_table == String(BIN_DATA_KEY)) {
                    const Variant v = bd[p_drop];
                    bd.erase(Variant(p_drop));
                    if(!bd.has(Variant(p_keep))) bd[Variant(p_keep)] = v;
                    mut = true;
                }
                if(td.has(Variant(p_drop)) && p_table == String(TEX_DATA_KEY)) {
                    const Variant v = td[p_drop];
                    td.erase(Variant(p_drop));
                    if(!td.has(Variant(p_keep))) td[Variant(p_keep)] = v;
                    mut = true;
                }
                if(mut) {
                    Dictionary ng = gdata;
                    ng[BIN_ROW_KEY] = bd;
                    ng[TEX_ROW_KEY] = td;
                    tryReplaceJsonQuotedStringInFile(m_assetPackPath.path_join(gnm), p_drop, p_keep);
                    const Ref<FileAccess> grf = FileAccess::open(m_assetPackPath.path_join(gnm), FileAccess::ModeFlags::READ);
                    if(grf.is_valid()) ng[WEIGHT_KEY] = int64_t(grf->get_length());
                    A[Variant(gnm)] = ng;
                }
            }
            m_assetsPackManifest[ASSETS_KEY] = A;
        }
        ttbl.erase(Variant(p_drop));
        m_assetsPackManifest[p_table] = ttbl;
        if(p_defer_delete) repairEnqueueDeletionRel(p_drop);
        else               deletePackRelativeFile(p_drop);
    }

    const bool Importer::mergeRepairDuplicateGltfPackFiles(const String& p_keep, const String& p_drop,
        bool p_defer_delete, bool p_already_equal) {
        if(!m_assetsPackManifest.has(ASSETS_KEY)) return false;
        Dictionary A = Dictionary(m_assetsPackManifest[ASSETS_KEY]);
        if(!A.has(Variant(p_keep)) || !A.has(Variant(p_drop))) return false;
        if(!p_already_equal && !fileBinaryEqual(m_assetPackPath.path_join(p_keep), m_assetPackPath.path_join(p_drop)))
            return false;
        A.erase(Variant(p_drop));
        m_assetsPackManifest[ASSETS_KEY] = A;
        const String dropPng = packRelativeCapturePngForGltfPackName(p_drop);
        if(p_defer_delete) {
            repairEnqueueDeletionRel(p_drop);
            repairEnqueueDeletionRel(dropPng);
        } else {
            deletePackRelativeFile(p_drop);
            deletePackRelativeFile(dropPng);
        }
        return true;
    }

    void Importer::listPackRootGltfFileNames(Array& p_out_base_names) {
        p_out_base_names.clear();
        if(m_assetPackPath.is_empty()) return;
        Array names;
        enumerateFilesInDirNonRecursive(m_assetPackPath, names);
        for(int i = 0; i < names.size(); ++i) {
            const String cur = String(names[i]);
            if(cur.to_lower().ends_with(".gltf")) p_out_base_names.append(cur);
        }
    }

}
