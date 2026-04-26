#include "Importer.h"

// Godot include
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;
namespace ImportExportModule {

    const float FPS_RATIO_BUDGET = 0.5f;
    const int PICTURE_MAKER_MAX = 8;

    void Importer::_bind_methods() {
        ClassDB::bind_method(
            D_METHOD("setupImportNewAssets", "asset_pack_path", "import_assets_path"), &Importer::setupImportNewAssets);
    }

    Importer::Importer() :
        m_importerState{E_ImporterState::Waiting},
        m_copySubPhase{E_CopySubPhase::Gltf},
        m_plannedNewBytes(0) {
    }

    Importer::~Importer() {
    }

    void Importer::_ready() {
        m_importerPictureMakerScene = ResourceLoader::get_singleton()->load(
            String(M_ModulesList.at("IMPORTER_PICTURE_MAKER").c_str()));
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
        if(m_importerState == E_ImporterState::Picturing) {
            const double k_framePictUsec
                = 1e6 * 1.0f / float(FPS_IN_CALCULATION) * FPS_RATIO_BUDGET;
            double timeBudget = k_framePictUsec;
            if(!isPicturingWorkDone()) {
                runPicturingPhase(timeBudget);
            }
            if(isPicturingWorkDone()) {
                m_importerState = E_ImporterState::Waiting;
            }
            return;
        }
        if(m_importerState != E_ImporterState::Copying) {
            return;
        }
        if(m_copySubPhase == E_CopySubPhase::SubDone) {
            return;
        }

        // Single per-frame time budget (us) for glTF then sidecar work; do not reset between sub-phases or
        // one frame can spend about 2x the intended budget.
        const double k_frameCopyUsec
            = 1e6 * 1.0f / float(FPS_IN_CALCULATION) * FPS_RATIO_BUDGET;
        double timeBudget = k_frameCopyUsec;
        if(m_copySubPhase == E_CopySubPhase::Gltf) {
            while(0.0 < timeBudget && 0 < m_plannedGltfItems.size()) {
                const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
                const Dictionary item = m_plannedGltfItems[0];
                m_plannedGltfItems.remove_at(0);
                if(!copyGltfPlannedItem(item)) {
                    m_importerState = E_ImporterState::Destruct;
                    return;
                }
                timeBudget -= (double)(Time::get_singleton()->get_ticks_usec() - t0);
            }
            if(0 < m_plannedGltfItems.size()) {
                return;
            }
            m_copySubPhase = E_CopySubPhase::Sidecar;
        }
        if(m_copySubPhase == E_CopySubPhase::Sidecar) {
            if(!runSidecarPhaseSlice(timeBudget)) {
                m_importerState = E_ImporterState::Destruct;
                return;
            }
            if(m_copySubPhase == E_CopySubPhase::Sidecar) {
                return;
            }
        }
        if(m_copySubPhase == E_CopySubPhase::Dedup) {
            if(!runDedupAndFinish()) {
                m_importerState = E_ImporterState::Destruct;
            }
        }
    }

    bool Importer::setupImportNewAssets(const String& p_assetPackPath, const String& p_importAssetsPath) {
        m_plannedGltfItems.clear();
        m_plannedSidecarItems.clear();
        m_plannedNewBytes = 0;
        m_copySubPhase = E_CopySubPhase::Gltf;
        m_gltfAssetsToCapturePath.clear();
        m_importRootWithSlash
            = p_importAssetsPath.ends_with("/") ? p_importAssetsPath : p_importAssetsPath + String("/");
        m_assetPackPath = p_assetPackPath;

        m_assetsPackManifest = getDictionaryFromJsonPath(p_assetPackPath.path_join("manifeste.json"));
        if(m_assetsPackManifest.is_empty()) {
            UtilityFunctions::push_error("Importer: setup, manifeste.json missing or invalid");
            m_importerState = E_ImporterState::Destruct;
            return false;
        }
        if(!isAssetsPackManifestValid(m_assetsPackManifest)) {
            UtilityFunctions::push_error("Importer: setup, manifeste.json missing required keys");
            m_importerState = E_ImporterState::Destruct;
            return false;
        }
        ensureManifestDefaultTables();

        Array found;
        if(!assetsImporterCalulation(p_importAssetsPath, found, 0)) {
            UtilityFunctions::push_error("Importer: setup, import path invalid or max depth exceeded");
            m_importerState = E_ImporterState::Destruct;
            return false;
        }
        if(found.is_empty()) {
            UtilityFunctions::push_error("Importer: setup, no .gltf under import path");
            m_importerState = E_ImporterState::Destruct;
            return false;
        }

        if(!buildImportPlan(found)) {
            m_importerState = E_ImporterState::Destruct;
            return false;
        }
        m_importerState = E_ImporterState::Copying;
        return true;
    }

    bool Importer::setupImportIIHexMapExportContent(const String& p_importAssetsPath) {
        (void)p_importAssetsPath;
        return false;
    }

    bool Importer::setupRepareAssetsPack(const String& p_assetsPackPath) {
        (void)p_assetsPackPath;
        return false;
    }

    const bool Importer::assetsImporterCalulation(const String& p_importPath, Array& p_gltfList, int p_currentDeep) {
        if(p_currentDeep > FILE_MANIPULATION_MAX_DEEP) {
            return false;
        }
        if(FileAccess::file_exists(p_importPath)) {
            if(p_importPath.to_lower().ends_with(".gltf")) {
                p_gltfList.append(p_importPath);
            }
            return true;
        }
        Ref<DirAccess> dir = DirAccess::open(p_importPath);
        if(dir.is_null()) {
            return false;
        }
        dir->list_dir_begin();
        String cur = dir->get_next();
        while(cur != String()) {
            if(cur != "." && cur != "..") {
                const String child = p_importPath.path_join(cur);
                if(dir->current_is_dir()) {
                    if(!assetsImporterCalulation(child, p_gltfList, p_currentDeep + 1)) {
                        dir->list_dir_end();
                        return false;
                    }
                } else if(cur.to_lower().ends_with(".gltf")) {
                    p_gltfList.append(child);
                }
            }
            cur = dir->get_next();
        }
        dir->list_dir_end();
        return true;
    }

    const bool Importer::buildImportPlan(const Array& p_gltfSourceList) {
        Dictionary reserved;
        collectReservedNamesFromPack(reserved);
        m_plannedGltfItems.clear();
        m_plannedSidecarItems.clear();
        m_plannedNewBytes = 0;
        // Source path -> final pack name for this run (simplified abs key).
        Dictionary sidecarBySource;
        for(int g = 0; g < p_gltfSourceList.size(); g++) {
            if(p_gltfSourceList[g].get_type() != Variant::Type::STRING) {
                return false;
            }
            const String gsrc = p_gltfSourceList[g];
            Ref<FileAccess> gf = FileAccess::open(gsrc, FileAccess::ModeFlags::READ);
            if(gf.is_null()) {
                return false;
            }
            const int64_t glen = (int64_t) gf->get_length();
            m_plannedNewBytes += (uint64_t) glen;
            Ref<JSON> j;
            j.instantiate();
            if(j->parse(gf->get_as_text()) != OK) {
                UtilityFunctions::push_error(String("Importer: plan, invalid JSON: ") + gsrc);
                return false;
            }
            const Variant vr = j->get_data();
            if(vr.get_type() != Variant::Type::DICTIONARY) {
                return false;
            }
            const Dictionary root = vr;
            const String base = gsrc.get_basename();
            const String adj = base + String(".bin");
            const bool hasAdj = FileAccess::file_exists(adj);
            Array ulist;
            collectGltfUris(gsrc, root, adj, hasAdj, ulist);
            for(int uu = 0; uu < ulist.size(); uu++) {
                const Dictionary um = ulist[uu];
                const String uri = String(um["uri"]);
                const bool isBuf = bool(um["is_buffer"]);
                if(!planOneSidecar(gsrc, uri, isBuf, sidecarBySource, reserved)) {
                    return false;
                }
            }
            const String wanted = gsrc.get_file();
            const String packG = makeUniqueNameInSet(wanted, reserved);
            const String tpath = toImportRootRelativePath(gsrc);
            const String ggroup = groupPathForSourceGltf(gsrc);
            Dictionary uriMap;
            Dictionary binD;
            Dictionary texD;
            for(int uu = 0; uu < ulist.size(); uu++) {
                const String uri = String(Dictionary(ulist[uu])["uri"]);
                const String absR = resolveGltfUriToAbsoluteFile(gsrc, uri);
                if(absR.is_empty()) {
                    continue;
                }
                const String skey = absR.simplify_path();
                if(!sidecarBySource.has(Variant(skey))) {
                    continue;
                }
                const String newName = String(sidecarBySource[Variant(skey)]);
                uriMap[Variant(uri)] = newName;
                const String tRel = toImportRootRelativePath(skey);
                const bool isBuf2 = ((Dictionary(ulist[uu])["is_buffer"])).booleanize();
                if(isBuf2) {
                    binD[Variant(newName)] = tRel;
                } else {
                    texD[Variant(newName)] = tRel;
                }
            }
            Dictionary item;
            item["source"] = gsrc;
            item["pack_name"] = packG;
            item["true_path"] = tpath;
            item["group"] = ggroup;
            item["gltf_size"] = (int64_t) glen;
            item["uri_to_pack"] = uriMap;
            item[manifest_key_gd(E_ManifestJsonKey::BIN)] = binD;
            item[manifest_key_gd(E_ManifestJsonKey::TEXTURE)] = texD;
            m_plannedGltfItems.append(item);
        }
        Ref<DirAccess> packD = DirAccess::open(m_assetPackPath);
        if(packD.is_null()) {
            return false;
        }
        if(!checkDiskSpace(m_plannedNewBytes)) {
            UtilityFunctions::push_error("Importer: plan, not enough disk space for the whole import");
            m_plannedGltfItems.clear();
            m_plannedSidecarItems.clear();
            return false;
        }
        return !m_plannedGltfItems.is_empty();
    }

    bool Importer::planOneSidecar(
        const String& p_gltfSrc,
        const String& p_uri,
        const bool p_isBuffer,
        Dictionary& p_srcToPackSoFar,
        Dictionary& p_reserved) {
        const String abs = resolveGltfUriToAbsoluteFile(p_gltfSrc, p_uri);
        if(abs.is_empty() || !FileAccess::file_exists(abs)) {
            return true;
        }
        const String simp = abs.simplify_path();
        if(p_srcToPackSoFar.has(Variant(simp))) {
            return true;
        }
        const String tRel = toImportRootRelativePath(simp);
        Ref<FileAccess> rf = FileAccess::open(simp, FileAccess::ModeFlags::READ);
        if(rf.is_null()) {
            return true;
        }
        const uint64_t sz = rf->get_length();
        const bool isBin = p_isBuffer; // user rule: from buffers table -> bin_data
        String reuse;
        if(fileExistsInPackByTruePath(tRel, sz, isBin, reuse)) {
            p_srcToPackSoFar[Variant(simp)] = reuse;
            return true;
        }
        const String pack
            = pickPackNameForNewSidecar(simp, isBin, sz, p_reserved);
        p_srcToPackSoFar[Variant(simp)] = pack;
        Dictionary en;
        en["source"] = simp;
        en["pack_name"] = pack;
        en["true_path"] = tRel;
        en["is_bin"] = isBin;
        en["size"] = (int64_t) sz;
        m_plannedSidecarItems.append(en);
        m_plannedNewBytes += sz;
        return true;
    }

    const bool Importer::checkDiskSpace(const uint64_t p_need) {
        const Ref<DirAccess> d = DirAccess::open(m_assetPackPath);
        if(d.is_null()) {
            return false;
        }
        const uint64_t sp = d->get_space_left();
        if(0 < sp && p_need + (1024ull * 1024) > sp) { // 1 Mo margin
            return p_need < sp;
        }
        if(0 < sp) {
            return p_need <= sp;
        }
        return true;
    }

    const bool Importer::copyGltfPlannedItem(const Dictionary& p_item) {
        const String src = p_item["source"];
        const String pnm = p_item["pack_name"];
        const String dest = m_assetPackPath.path_join(pnm);
        if(DirAccess::copy_absolute(src, dest) != OK) {
            UtilityFunctions::push_error(String("Importer: copy gltf failed: ") + src);
            return false;
        }
        const Dictionary uriToPack = p_item["uri_to_pack"];
        Ref<FileAccess> rf = FileAccess::open(dest, FileAccess::ModeFlags::READ);
        if(rf.is_null()) {
            return false;
        }
        String out = rf->get_as_text();
        sortDedupAndApplyUriStrings(out, uriToPack);
        const Ref<FileAccess> wf = FileAccess::open(dest, FileAccess::ModeFlags::WRITE);
        if(wf.is_null()) {
            return false;
        }
        wf->store_string(out);
        recordGltfRowInManifest(p_item);
        m_gltfAssetsToCapturePath.append(dest);
        return true;
    }

    void Importer::recordGltfRowInManifest(const Dictionary& p_item) {
        const String key = p_item["pack_name"];
        Dictionary a = m_assetsPackManifest.has("assets_data") ? Dictionary(m_assetsPackManifest["assets_data"]) : Dictionary();
        Dictionary row;
        row[manifest_key_gd(E_ManifestJsonKey::GROUP)] = p_item["group"];
        row[manifest_key_gd(E_ManifestJsonKey::TRUE_PATH)] = p_item["true_path"];
        row[manifest_key_gd(E_ManifestJsonKey::BIN)]
            = p_item.has(manifest_key_gd(E_ManifestJsonKey::BIN)) ? p_item[manifest_key_gd(E_ManifestJsonKey::BIN)] : Variant(Dictionary());
        row[manifest_key_gd(E_ManifestJsonKey::TEXTURE)]
            = p_item.has(manifest_key_gd(E_ManifestJsonKey::TEXTURE)) ? p_item[manifest_key_gd(E_ManifestJsonKey::TEXTURE)] : Variant(Dictionary());
        row[manifest_key_gd(E_ManifestJsonKey::WEIGHT)] = p_item["gltf_size"];
        a[Variant(key)] = row;
        m_assetsPackManifest["assets_data"] = a;
        if(!p_item["group"].operator String().is_empty()) {
            const String gp = p_item["group"];
            Array g = m_assetsPackManifest.has("groups") ? Array(m_assetsPackManifest["groups"]) : Array();
            bool hasG = false;
            for(int gi = 0; gi < g.size(); gi++) {
                if(String(g[gi]) == gp) {
                    hasG = true;
                    break;
                }
            }
            if(!hasG) {
                g.append(gp);
            }
            m_assetsPackManifest["groups"] = g;
        }
    }

    const bool Importer::runSidecarPhaseSlice(double& io_timeBudgetUsec) {
        if(m_plannedSidecarItems.is_empty()) {
            m_copySubPhase = E_CopySubPhase::Dedup;
            return true;
        }
        while(0.0 < io_timeBudgetUsec && 0 < m_plannedSidecarItems.size()) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const Dictionary d = m_plannedSidecarItems[0];
            m_plannedSidecarItems.remove_at(0);
            const String s = d["source"];
            const String p = d["pack_name"];
            const String tr = d["true_path"];
            const int64_t sz = d["size"];
            const bool isb = d["is_bin"];
            const String de = m_assetPackPath.path_join(p);
            if(DirAccess::copy_absolute(s, de) != OK) {
                UtilityFunctions::push_error(String("Importer: sidecar copy failed: ") + s);
                return false;
            }
            const String tname = isb ? "bin_data" : "texture_data";
            Dictionary T = m_assetsPackManifest.has(tname) ? Dictionary(m_assetsPackManifest[tname]) : Dictionary();
            Dictionary row;
            row[manifest_key_gd(E_ManifestJsonKey::TRUE_PATH)] = tr;
            row[manifest_key_gd(E_ManifestJsonKey::WEIGHT)] = sz;
            T[Variant(p)] = row;
            m_assetsPackManifest[tname] = T;
            io_timeBudgetUsec -= (double)(Time::get_singleton()->get_ticks_usec() - t0);
        }
        if(0 < m_plannedSidecarItems.size()) {
            return true;
        }
        m_copySubPhase = E_CopySubPhase::Dedup;
        return true;
    }

    const bool Importer::runDedupAndFinish() {
        deduplicateSidecarsInManifest();
        deduplicateGltfAssetsInManifest();
        recomputeGlobalSizesAndCount();
        writeManifestToDisk();
        m_copySubPhase = E_CopySubPhase::SubDone;
        m_importerState = E_ImporterState::Picturing;
        return true;
    }

    void Importer::deduplicateSidecarsInManifest() {
        deduplicateOneSidecarTable("bin_data");
        deduplicateOneSidecarTable("texture_data");
    }

    void Importer::deduplicateOneSidecarTable(const String& p_tableName) {
        if(!m_assetsPackManifest.has(p_tableName)) {
            return;
        }
        const String sBin = String("bin_data");
        const String sTex = String("texture_data");
        const String sTrue = manifest_key_gd(E_ManifestJsonKey::TRUE_PATH);
        const String sW = manifest_key_gd(E_ManifestJsonKey::WEIGHT);
        const String sB = manifest_key_gd(E_ManifestJsonKey::BIN);
        const String sT = manifest_key_gd(E_ManifestJsonKey::TEXTURE);
        Dictionary T = m_assetsPackManifest[p_tableName];
        const Array keys = T.keys();
        // Group by (true_path, weight) -> list of pack file names
        Dictionary groupKeyToNames;
        for(int i = 0; i < keys.size(); i++) {
            const String pk = String(keys[i]);
            if(!T.has(Variant(pk))) {
                continue;
            }
            const Dictionary row = T[pk];
            if(!row.has(sTrue) || !row.has(sW)) {
                continue;
            }
            const String gk = String(row[sTrue]) + String("||") + String::num_int64((int64_t) row[sW]);
            Array names;
            if(groupKeyToNames.has(Variant(gk))) {
                names = groupKeyToNames[Variant(gk)];
            }
            {
                bool foundPk = false;
                for(int vi = 0; vi < names.size(); vi++) {
                    if(names[vi] == Variant(pk)) {
                        foundPk = true;
                        break;
                    }
                }
                if(!foundPk) {
                    names.append(pk);
                }
            }
            groupKeyToNames[Variant(gk)] = names;
        }
        const Array gks = groupKeyToNames.keys();
        for(int gi = 0; gi < gks.size(); gi++) {
            const Array nms = groupKeyToNames[gks[gi]];
            if(nms.size() < 2) {
                continue;
            }
            const String canon = String(nms[0]);
            for(int j = 1; j < nms.size(); j++) {
                const String dup = String(nms[j]);
                const String pa = m_assetPackPath.path_join(canon);
                const String pb = m_assetPackPath.path_join(dup);
                if(!fileBinaryEqual(pa, pb)) {
                    continue;
                }
                if(m_assetsPackManifest.has("assets_data")) {
                    Dictionary A = m_assetsPackManifest["assets_data"];
                    const Array ak = A.keys();
                    for(int a = 0; a < ak.size(); a++) {
                        const String gname = String(ak[a]);
                        if(!A.has(Variant(gname))) {
                            continue;
                        }
                        const Dictionary gdata = A[gname];
                        Dictionary bd
                            = gdata.has(sB) && gdata[sB].get_type() == Variant::Type::DICTIONARY
                            ? gdata[sB]
                            : Dictionary();
                        Dictionary td
                            = gdata.has(sT) && gdata[sT].get_type() == Variant::Type::DICTIONARY
                            ? gdata[sT]
                            : Dictionary();
                        bool ch = false;
                        if(bd.has(Variant(dup)) && p_tableName == sBin) {
                            const Variant v = bd[Variant(dup)];
                            bd.erase(Variant(dup));
                            if(!bd.has(Variant(canon))) {
                                bd[Variant(canon)] = v;
                            }
                            ch = true;
                        }
                        if(td.has(Variant(dup)) && p_tableName == sTex) {
                            const Variant v = td[Variant(dup)];
                            td.erase(Variant(dup));
                            if(!td.has(Variant(canon))) {
                                td[Variant(canon)] = v;
                            }
                            ch = true;
                        }
                        if(ch) {
                            Dictionary ng = gdata;
                            ng[sB] = bd;
                            ng[sT] = td;
                            A[Variant(gname)] = ng;
                            tryReplaceJsonQuotedStringInFile(m_assetPackPath.path_join(gname), dup, canon);
                        }
                    }
                    m_assetsPackManifest["assets_data"] = A;
                }
                T.erase(Variant(dup));
                if(FileAccess::file_exists(m_assetPackPath.path_join(dup))) {
                    const Ref<DirAccess> d = DirAccess::open(m_assetPackPath);
                    if(d.is_valid()) {
                        d->remove(dup);
                    }
                }
            }
        }
        m_assetsPackManifest[p_tableName] = T;
    }
    void Importer::deduplicateGltfAssetsInManifest() {
        if(!m_assetsPackManifest.has("assets_data")) {
            return;
        }
        bool progress = true;
        while(progress) {
            progress = false;
            Dictionary A = m_assetsPackManifest["assets_data"];
            const Array k = A.keys();
            bool merged = false;
            for(int i = 0; i < k.size() - 1 && !merged; i++) {
                for(int j = i + 1; j < k.size() && !merged; j++) {
                    const String a = String(k[i]);
                    const String b = String(k[j]);
                    if(!A.has(Variant(a)) || !A.has(Variant(b))) {
                        continue;
                    }
                    const Dictionary da = A[a];
                    const Dictionary db = A[b];
                    if(String(da[manifest_key_gd(E_ManifestJsonKey::TRUE_PATH)]) != String(db[manifest_key_gd(E_ManifestJsonKey::TRUE_PATH)])) {
                        continue;
                    }
                    const Dictionary dab = da.has(manifest_key_gd(E_ManifestJsonKey::BIN)) && da[manifest_key_gd(E_ManifestJsonKey::BIN)].get_type() == Variant::Type::DICTIONARY
                        ? Dictionary(da[manifest_key_gd(E_ManifestJsonKey::BIN)])
                        : Dictionary();
                    const Dictionary dbb = db.has(manifest_key_gd(E_ManifestJsonKey::BIN)) && db[manifest_key_gd(E_ManifestJsonKey::BIN)].get_type() == Variant::Type::DICTIONARY
                        ? Dictionary(db[manifest_key_gd(E_ManifestJsonKey::BIN)])
                        : Dictionary();
                    const Dictionary dat = da.has(manifest_key_gd(E_ManifestJsonKey::TEXTURE)) && da[manifest_key_gd(E_ManifestJsonKey::TEXTURE)].get_type() == Variant::Type::DICTIONARY
                        ? Dictionary(da[manifest_key_gd(E_ManifestJsonKey::TEXTURE)])
                        : Dictionary();
                    const Dictionary dbt = db.has(manifest_key_gd(E_ManifestJsonKey::TEXTURE)) && db[manifest_key_gd(E_ManifestJsonKey::TEXTURE)].get_type() == Variant::Type::DICTIONARY
                        ? Dictionary(db[manifest_key_gd(E_ManifestJsonKey::TEXTURE)])
                        : Dictionary();
                    if(!dictEqualShallow(dab, dbb) || !dictEqualShallow(dat, dbt)) {
                        continue;
                    }
                    const String pa = m_assetPackPath.path_join(a);
                    const String pb = m_assetPackPath.path_join(b);
                    if(!fileBinaryEqual(pa, pb)) {
                        continue;
                    }
                    A.erase(Variant(b));
                    m_assetsPackManifest["assets_data"] = A;
                    if(FileAccess::file_exists(pb)) {
                        const Ref<DirAccess> d = DirAccess::open(m_assetPackPath);
                        if(d.is_valid()) {
                            d->remove(b);
                        }
                    }
                    for(int c = 0; c < m_gltfAssetsToCapturePath.size(); c++) {
                        if(m_gltfAssetsToCapturePath[c].operator String() == pb) {
                            m_gltfAssetsToCapturePath.remove_at(c);
                            break;
                        }
                    }
                    merged = true;
                    progress = true;
                }
            }
        }
    }
    uint64_t Importer::computeTotalWeightBytes() {
        uint64_t tot = 0;
        const String wk = manifest_key_gd(E_ManifestJsonKey::WEIGHT);
        if(m_assetsPackManifest.has("assets_data")) {
            const Dictionary A = m_assetsPackManifest["assets_data"];
            const Array k = A.keys();
            for(int i = 0; i < k.size(); i++) {
                const Dictionary d = A[k[i]];
                if(d.has(wk)) {
                    tot += (uint64_t) d[wk];
                }
            }
        }
        for(int t = 0; t < 2; t++) {
            const String tname = t == 0 ? "bin_data" : "texture_data";
            if(!m_assetsPackManifest.has(tname)) {
                continue;
            }
            const Dictionary T = m_assetsPackManifest[tname];
            const Array kk = T.keys();
            for(int i = 0; i < kk.size(); i++) {
                const Dictionary d = T[kk[i]];
                if(d.has(wk)) {
                    tot += (uint64_t) d[wk];
                }
            }
        }
        return tot;
    }

    void Importer::recomputeGlobalSizesAndCount() {
        const uint64_t tot = computeTotalWeightBytes();
        m_assetsPackManifest["poid_bytes"] = (int64_t) tot;
        m_assetsPackManifest["poid"] = String::num(double(tot) / (1024.0 * 1024.0), 3) + " Mo";
        m_assetsPackManifest["asset"] = m_assetsPackManifest.has("assets_data")
            ? int64_t(Dictionary(m_assetsPackManifest["assets_data"]).size())
            : 0;
    }

    void Importer::writeManifestToDisk() {
        m_assetsPackManifest["date"] = Time::get_singleton()->get_datetime_dict_from_system();
        const String p = m_assetPackPath.path_join("manifeste.json");
        const Ref<FileAccess> w = FileAccess::open(p, FileAccess::ModeFlags::WRITE);
        if(w.is_null()) {
            return;
        }
        w->store_string(JSON::stringify(m_assetsPackManifest, "\t"));
    }

    void Importer::ensureManifestDefaultTables() {
        if(!m_assetsPackManifest.has("assets_data")) {
            m_assetsPackManifest["assets_data"] = Dictionary();
        }
        if(!m_assetsPackManifest.has("bin_data")) {
            m_assetsPackManifest["bin_data"] = Dictionary();
        }
        if(!m_assetsPackManifest.has("texture_data")) {
            m_assetsPackManifest["texture_data"] = Dictionary();
        }
        if(!m_assetsPackManifest.has("groups")) {
            m_assetsPackManifest["groups"] = Array();
        }
    }

    const bool Importer::isAssetsPackManifestValid(const Dictionary& p_manifest) {
        return p_manifest.has("nom") && p_manifest.has("version") && p_manifest.has("poid") && p_manifest.has("asset");
    }

    const Dictionary Importer::getDictionaryFromJsonPath(const String& p_path) {
        if(!FileAccess::file_exists(p_path)) {
            return {};
        }
        Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
        if(f.is_null()) {
            return {};
        }
        Ref<JSON> j;
        j.instantiate();
        if(j->parse(f->get_as_text()) != OK) {
            return {};
        }
        const Variant v = j->get_data();
        if(v.get_type() != Variant::Type::DICTIONARY) {
            return {};
        }
        return v;
    }

    void Importer::addDictionaryKeysToReserved(const Dictionary& p_dict, Dictionary& p_outReserved) {
        const Array k = p_dict.keys();
        for(int i = 0; i < k.size(); i++) {
            p_outReserved[Variant(String(k[i]))] = true;
        }
    }

    void Importer::collectReservedNamesFromPack(Dictionary& p_outReserved) {
        const Dictionary a = m_assetsPackManifest.has("assets_data")
            ? Dictionary(m_assetsPackManifest["assets_data"])
            : Dictionary();
        addDictionaryKeysToReserved(a, p_outReserved);
        addDictionaryKeysToReserved(
            m_assetsPackManifest.has("bin_data") ? Dictionary(m_assetsPackManifest["bin_data"]) : Dictionary(),
            p_outReserved);
        addDictionaryKeysToReserved(
            m_assetsPackManifest.has("texture_data") ? Dictionary(m_assetsPackManifest["texture_data"]) : Dictionary(),
            p_outReserved);
        if(!m_assetPackPath.is_empty()) {
            Ref<DirAccess> root = DirAccess::open(m_assetPackPath);
            if(!root.is_null()) {
                root->list_dir_begin();
                String n = root->get_next();
                while(n != String()) {
                    if(n != "." && n != ".." && !root->current_is_dir()) {
                        p_outReserved[Variant(n)] = true;
                    }
                    n = root->get_next();
                }
                root->list_dir_end();
            }
        }
    }

    const String Importer::toImportRootRelativePath(const String& p_abs) {
        if(p_abs.begins_with(m_importRootWithSlash)) {
            return p_abs.substr(m_importRootWithSlash.length()).replace("\\", "/");
        }
        return p_abs.get_file();
    }

    const String Importer::groupPathForSourceGltf(const String& p_srcGltf) {
        String relative = p_srcGltf.get_file();
        if(p_srcGltf.begins_with(m_importRootWithSlash)) {
            relative = p_srcGltf.substr(m_importRootWithSlash.length());
        }
        return relative.get_base_dir() == "." ? String("") : String(relative.get_base_dir());
    }

    const String Importer::makeUniqueNameInSet(const String& p_preferred, Dictionary& p_reserved) {
        if(!p_preferred.is_empty() && !p_reserved.has(Variant(p_preferred))) {
            p_reserved[Variant(p_preferred)] = true;
            return p_preferred;
        }
        const String ext = p_preferred.get_extension();
        const String stem = p_preferred.get_basename();
        int d = 2;
        for(;;) {
            const String c = stem + String("_") + String::num_int64(d) + (ext.is_empty() ? String() : (String(".") + ext));
            d++;
            if(10000 < d) {
                return p_preferred; // should not happen
            }
            if(!p_reserved.has(Variant(c))) {
                p_reserved[Variant(c)] = true;
                return c;
            }
        }
    }

    const bool Importer::fileExistsInPackByTruePath(
        const String& p_truePathRel, uint64_t p_size, bool p_isBin, String& p_outName) {
        const String tableName = p_isBin ? "bin_data" : "texture_data";
        if(!m_assetsPackManifest.has(tableName)) {
            return false;
        }
        const Dictionary t = m_assetsPackManifest[tableName];
        const String kTrue = manifest_key_gd(E_ManifestJsonKey::TRUE_PATH);
        const String kW = manifest_key_gd(E_ManifestJsonKey::WEIGHT);
        const Array keys = t.keys();
        for(int i = 0; i < keys.size(); i++) {
            const String key = String(keys[i]);
            if(!t.has(Variant(key))) {
                continue;
            }
            const Dictionary row = t[key];
            if(!row.has(kTrue) || !row.has(kW)) {
                continue;
            }
            if(String(row[kTrue]) != p_truePathRel) {
                continue;
            }
            if((uint64_t) row[kW] != p_size) {
                continue;
            }
            p_outName = key;
            return true;
        }
        return false;
    }

    const String Importer::pickPackNameForNewSidecar(
        const String& p_sourceAbs, bool p_isBin, uint64_t p_size, Dictionary& p_reserved) {
        (void)p_size;
        const String fileOnly = p_sourceAbs.get_file();
        const String ext = fileOnly.get_extension();
        const String base = p_isBin && ext.to_lower() == "bin" ? fileOnly
                           : p_isBin ? (fileOnly.get_basename() + String(".bin"))
                                     : fileOnly;
        return makeUniqueNameInSet(base, p_reserved);
    }

    void Importer::appendGltfJsonSectionUris(
        const Dictionary& p_root, const char* p_key, bool p_isBuffer, Array& r_out) {
        if(!p_root.has(p_key) || p_root[p_key].get_type() != Variant::Type::ARRAY) {
            return;
        }
        const Array arr = p_root[p_key];
        for(int i = 0; i < arr.size(); i++) {
            if(arr[i].get_type() != Variant::Type::DICTIONARY) {
                continue;
            }
            const Dictionary row = arr[i];
            if(!row.has("uri")) {
                continue;
            }
            const String u = String(row["uri"]);
            if(gltfUriCannotMapToLocalFile(u)) {
                continue;
            }
            Dictionary m;
            m["uri"] = u;
            m["is_buffer"] = p_isBuffer;
            r_out.append(m);
        }
    }

    const bool Importer::collectGltfUris(
        const String& p_srcGltf,
        const Dictionary& p_root,
        const String& p_adjAbs,
        const bool p_hasAdj,
        Array& r_out) {
        appendGltfJsonSectionUris(p_root, "buffers", true, r_out);
        appendGltfJsonSectionUris(p_root, "images", false, r_out);
        if(p_hasAdj && !p_adjAbs.is_empty()) {
            bool have = false;
            for(int j = 0; j < r_out.size() && !have; j++) {
                const Dictionary d = r_out[j];
                const String u = String(d["uri"]);
                if(resolveGltfUriToAbsoluteFile(p_srcGltf, u) == p_adjAbs) {
                    have = true;
                }
            }
            if(!have) {
                Dictionary m;
                m["uri"] = p_adjAbs.get_file();
                m["is_buffer"] = true;
                r_out.append(m);
            }
        }
        return true;
    }

    const bool Importer::gltfUriCannotMapToLocalFile(const String& p_uri) {
        return p_uri.is_empty() || p_uri.begins_with("data:") || p_uri.begins_with("file://")
            || (0 <= p_uri.find("://") && !p_uri.begins_with("file://"));
    }

    const String Importer::resolveGltfUriToAbsoluteFile(const String& p_srcGltf, const String& p_uri) {
        if(gltfUriCannotMapToLocalFile(p_uri)) {
            return {};
        }
        const String base = p_srcGltf.get_base_dir();
        if(p_uri.is_relative_path()) {
            return (base.path_join(p_uri)).simplify_path();
        }
        return p_uri.simplify_path();
    }

    const String Importer::gltfJsonQuotedStringToken(const String& p_unescaped) {
        return JSON::stringify(Variant(p_unescaped), String(), false, true);
    }

    const String Importer::manifest_key_gd(const E_ManifestJsonKey p_key) const
    {
        return String(M_MANIFEST_JSON_KEY_STRINGS.at(p_key).c_str());
    }
    void Importer::sortDedupAndApplyUriStrings(String& p_text, const Dictionary& p_uriToName) {
        Array pairs;
        {
            const Array ks = p_uriToName.keys();
            for(int i = 0; i < ks.size(); i++) {
                const String oldU = String(ks[i]);
                const String newF = String(p_uriToName[Variant(oldU)]);
                if(oldU == newF) {
                    continue;
                }
                Array p;
                p.append(oldU);
                p.append(newF);
                pairs.append(p);
            }
        }
        {
            Dictionary seen;
            Array out;
            for(int i = 0; i < pairs.size(); i++) {
                const Array pr = pairs[i];
                if(pr.size() < 2) {
                    continue;
                }
                const String o = String(pr[0]);
                if(seen.has(Variant(o))) {
                    continue;
                }
                seen[Variant(o)] = true;
                out.append(pr);
            }
            pairs = out;
        }
        {
            const int n = (int) pairs.size();
            for(int a = 0; a < n - 1; a++) {
                for(int b = 0; b < n - 1; b++) {
                    const Array pa = pairs[b];
                    const Array pb = pairs[b + 1];
                    if((int) pa.size() < 2 || (int) pb.size() < 2) {
                        continue;
                    }
                    if(String(pb[0]).length() > String(pa[0]).length()) {
                        const Variant t = pairs[b];
                        pairs[b] = pairs[b + 1];
                        pairs[b + 1] = t;
                    }
                }
            }
        }
        for(int i = 0; i < pairs.size(); i++) {
            const Array pr = pairs[i];
            if(pr.size() < 2) {
                continue;
            }
            const String o = String(pr[0]);
            const String n = String(pr[1]);
            if(o == n) {
                continue;
            }
            const String oq = JSON::stringify(Variant(o), String(), false, true);
            const String nq = JSON::stringify(Variant(n), String(), false, true);
            if(p_text.find(oq) < 0) {
                UtilityFunctions::push_warning(
                    String("Importer: gltf uri not found in file text, skipped: ") + o);
                continue;
            }
            p_text = p_text.replace(oq, nq);
        }
    }

    const bool Importer::fileBinaryEqual(const String& p_a, const String& p_b) {
        if(!FileAccess::file_exists(p_a) || !FileAccess::file_exists(p_b)) {
            return false;
        }
        Ref<FileAccess> fa = FileAccess::open(p_a, FileAccess::ModeFlags::READ);
        Ref<FileAccess> fb = FileAccess::open(p_b, FileAccess::ModeFlags::READ);
        if(fa.is_null() || fb.is_null()) {
            return false;
        }
        const int64_t n = (int64_t) fa->get_length();
        if(n != (int64_t) fb->get_length()) {
            return false;
        }
        if(n == 0) {
            return true;
        }
        const PackedByteArray ca = fa->get_buffer(n);
        const PackedByteArray cb = fb->get_buffer(n);
        return ca == cb;
    }

    const bool Importer::dictEqualShallow(const Dictionary& p_a, const Dictionary& p_b) {
        if(p_a.size() != p_b.size()) {
            return false;
        }
        const Array ka = p_a.keys();
        for(int i = 0; i < ka.size(); i++) {
            const String k = String(ka[i]);
            if(!p_b.has(Variant(k))) {
                return false;
            }
            if(p_a[Variant(k)] != p_b[Variant(k)]) {
                return false;
            }
        }
        return true;
    }

    bool Importer::tryReplaceJsonQuotedStringInFile(
        const String& p_path, const String& p_oldB, const String& p_newB) {
        if(p_oldB == p_newB) {
            return true;
        }
        Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
        if(f.is_null()) {
            return false;
        }
        String t = f->get_as_text();
        const String oq = gltfJsonQuotedStringToken(p_oldB);
        const String nq = gltfJsonQuotedStringToken(p_newB);
        if(t.find(oq) < 0) {
            return true;
        }
        t = t.replace(oq, nq);
        const Ref<FileAccess> w = FileAccess::open(p_path, FileAccess::ModeFlags::WRITE);
        if(w.is_null()) {
            return false;
        }
        w->store_string(t);
        return true;
    }

    bool Importer::isPicturingWorkDone() {
        return m_gltfAssetsToCapturePath.is_empty() && m_activePictureMakers.is_empty();
    }

    void Importer::runPicturingPhase(double& io_timeBudgetUsec) {
        if(m_importerPictureMakerScene.is_null()) {
            return;
        }
        while(m_idlePictureMakers.size() + m_activePictureMakers.size() < PICTURE_MAKER_MAX && 0.0 < io_timeBudgetUsec) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            Node* n = m_importerPictureMakerScene->instantiate();
            if(n == nullptr) {
                UtilityFunctions::push_error("Importer: picturing, instantiate ImporterPictureMaker failed");
                break;
            }
            ImporterPictureMaker* ipm = Object::cast_to<ImporterPictureMaker>(n);
            if(ipm == nullptr) {
                UtilityFunctions::push_error(
                    String("Importer: picturing, scene root is not ImporterPictureMaker: ") + n->get_class());
                n->queue_free();
                io_timeBudgetUsec -= (double)(Time::get_singleton()->get_ticks_usec() - t0);
                break;
            }
            add_child(ipm);
            m_idlePictureMakers.append(ipm);
            io_timeBudgetUsec -= (double)(Time::get_singleton()->get_ticks_usec() - t0);
        }
        for(int i = m_activePictureMakers.size() - 1; 0 <= i && 0.0 < io_timeBudgetUsec; i--) {
            Object* o = m_activePictureMakers[i].operator Object*();
            if(o == nullptr) {
                UtilityFunctions::push_error(
                    String("Importer: picturing, null Object* in m_activePictureMakers at index ")
                        + String::num_int64((int64_t)i));
                m_activePictureMakers.remove_at(i);
                continue;
            }
            ImporterPictureMaker* w = Object::cast_to<ImporterPictureMaker>(o);
            if(w == nullptr) {
                UtilityFunctions::push_error(
                    String("Importer: picturing, m_activePictureMakers[") + String::num_int64((int64_t)i)
                        + String("] is not ImporterPictureMaker, got: ") + o->get_class());
                if(Node* bad = Object::cast_to<Node>(o)) {
                    bad->queue_free();
                }
                m_activePictureMakers.remove_at(i);
                continue;
            }
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const ImporterPictureMaker::E_ImporterPictureMakerState s = w->stepProgress();
            io_timeBudgetUsec -= (double)(Time::get_singleton()->get_ticks_usec() - t0);
            if(s == ImporterPictureMaker::E_ImporterPictureMakerState::Waiting) {
                m_activePictureMakers.remove_at(i);
                m_idlePictureMakers.append(w);
            }
        }
        while(0.0 < io_timeBudgetUsec && 0 < m_gltfAssetsToCapturePath.size() && 0 < m_idlePictureMakers.size()) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const int lasti = m_idlePictureMakers.size() - 1;
            Object* o = m_idlePictureMakers[lasti].operator Object*();
            if(o == nullptr) {
                UtilityFunctions::push_error(
                    String("Importer: picturing, null Object* in m_idlePictureMakers at index ")
                        + String::num_int64((int64_t)lasti));
                m_idlePictureMakers.remove_at(lasti);
                io_timeBudgetUsec -= (double)(Time::get_singleton()->get_ticks_usec() - t0);
                continue;
            }
            ImporterPictureMaker* ipm = Object::cast_to<ImporterPictureMaker>(o);
            if(ipm == nullptr) {
                UtilityFunctions::push_error(
                    String("Importer: picturing, m_idlePictureMakers[") + String::num_int64((int64_t)lasti)
                        + String("] is not ImporterPictureMaker, got: ") + o->get_class());
                if(Node* bad = Object::cast_to<Node>(o)) {
                    bad->queue_free();
                }
                m_idlePictureMakers.remove_at(lasti);
                io_timeBudgetUsec -= (double)(Time::get_singleton()->get_ticks_usec() - t0);
                continue;
            }
            m_idlePictureMakers.remove_at(lasti);
            const String path = m_gltfAssetsToCapturePath[0];
            m_gltfAssetsToCapturePath.remove_at(0);
            ipm->makeAPicture(path);
            m_activePictureMakers.append(ipm);
            io_timeBudgetUsec -= (double)(Time::get_singleton()->get_ticks_usec() - t0);
        }
    }
}
