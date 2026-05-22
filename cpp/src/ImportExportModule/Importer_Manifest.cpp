#include "Importer.h"
#include "Importer_Constants.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace ImportExportModule {

    const Dictionary Importer::manifestAssetsDict() {
        const Variant v = m_assetsPackManifest.has(ASSETS_KEY) ? m_assetsPackManifest[ASSETS_KEY] : Variant();
        return v.get_type() == Variant::Type::DICTIONARY ? Dictionary(v) : Dictionary();
    }

    const Dictionary Importer::manifestBinDict() {
        const Variant v = m_assetsPackManifest.has(BIN_DATA_KEY) ? m_assetsPackManifest[BIN_DATA_KEY] : Variant();
        return v.get_type() == Variant::Type::DICTIONARY ? Dictionary(v) : Dictionary();
    }

    const Dictionary Importer::manifestTexDict() {
        const Variant v = m_assetsPackManifest.has(TEX_DATA_KEY) ? m_assetsPackManifest[TEX_DATA_KEY] : Variant();
        return v.get_type() == Variant::Type::DICTIONARY ? Dictionary(v) : Dictionary();
    }

    const Array Importer::manifestGroupsArray() {
        const Variant v = m_assetsPackManifest.has(GROUPS_KEY) ? m_assetsPackManifest[GROUPS_KEY] : Variant();
        return v.get_type() == Variant::Type::ARRAY ? Array(v) : Array();
    }

    const Dictionary Importer::makeSidecarRow(const String& p_true_path, int64_t p_weight) {
        Dictionary row;
        row[TRUE_PATH_KEY] = p_true_path;
        row[WEIGHT_KEY]    = p_weight;
        return row;
    }

    void Importer::ensureManifestDefaultTables() {
        if(!m_assetsPackManifest.has(ASSETS_KEY))   m_assetsPackManifest[ASSETS_KEY]   = Dictionary();
        if(!m_assetsPackManifest.has(BIN_DATA_KEY)) m_assetsPackManifest[BIN_DATA_KEY] = Dictionary();
        if(!m_assetsPackManifest.has(TEX_DATA_KEY)) m_assetsPackManifest[TEX_DATA_KEY] = Dictionary();
        if(!m_assetsPackManifest.has(GROUPS_KEY))   m_assetsPackManifest[GROUPS_KEY]   = Array();
    }

    const bool Importer::isAssetsPackManifestValid(const Dictionary& p_manifest) {
        return p_manifest.has("nom") && p_manifest.has("version")
            && p_manifest.has("poid") && p_manifest.has("asset");
    }

    const Dictionary Importer::getDictionaryFromJsonPath(const String& p_path) {
        if(!FileAccess::file_exists(p_path)) return {};
        const Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
        if(f.is_null()) return {};
        Ref<JSON> j;
        j.instantiate();
        if(j->parse(f->get_as_text()) != OK) return {};
        const Variant v = j->get_data();
        return v.get_type() == Variant::Type::DICTIONARY ? Dictionary(v) : Dictionary();
    }

    void Importer::ensureManifestSidecarRow(const String& p_table, const String& p_key, const String& p_true_path, int64_t p_weight) {
        ensureManifestDefaultTables();
        Dictionary tbl = Dictionary(m_assetsPackManifest[p_table]);
        if(tbl.has(Variant(p_key))) return;
        tbl[Variant(p_key)] = makeSidecarRow(p_true_path, p_weight);
        m_assetsPackManifest[p_table] = tbl;
    }

    const bool Importer::loadPackManifestFromDiskOrDestruct(const String& p_pack_root, const String& p_context) {
        m_assetPackPath      = p_pack_root;
        m_assetsPackManifest = getDictionaryFromJsonPath(p_pack_root.path_join(String(M_MANIFEST_JSON_FILENAME.c_str())));
        if(m_assetsPackManifest.is_empty()) {
            UtilityFunctions::push_error(String("Importer: ") + p_context + String(", ")
                + String(M_MANIFEST_JSON_FILENAME.c_str()) + String(" missing or invalid"));
            m_importerState = E_ImporterState::Destruct;
            return false;
        }
        if(!isAssetsPackManifestValid(m_assetsPackManifest)) {
            UtilityFunctions::push_error(String("Importer: ") + p_context + String(", ")
                + String(M_MANIFEST_JSON_FILENAME.c_str()) + String(" missing required keys"));
            m_importerState = E_ImporterState::Destruct;
            return false;
        }
        ensureManifestDefaultTables();
        return true;
    }

    const bool Importer::commitPackManifestToDisk(bool p_prune_empty_groups) {
        if(p_prune_empty_groups) pruneEmptyGroupsInManifest();
        recomputeGlobalSizesAndCount();
        return writeManifestToDisk();
    }

    const bool Importer::writeManifestToDisk() {
        m_assetsPackManifest["date"] = Time::get_singleton()->get_datetime_dict_from_system();
        const String p = m_assetPackPath.path_join(String(M_MANIFEST_JSON_FILENAME.c_str()));
        const Ref<FileAccess> w = FileAccess::open(p, FileAccess::ModeFlags::WRITE);
        if(w.is_null()) {
            UtilityFunctions::push_error(String("Importer: cannot open ")
                + String(M_MANIFEST_JSON_FILENAME.c_str()) + String(" for write: ") + p);
            return false;
        }
        w->store_string(JSON::stringify(m_assetsPackManifest, "\t"));
        return true;
    }

    void Importer::recomputeGlobalSizesAndCount() {
        const uint64_t tot = computeTotalWeightBytes();
        m_assetsPackManifest["poid_bytes"] = int64_t(tot);
        m_assetsPackManifest["poid"]       = String::num(double(tot) / (1024.0 * 1024.0), 3) + " Mo";
        m_assetsPackManifest["asset"]      = int64_t(manifestAssetsDict().size());
    }

    const uint64_t Importer::computeTotalWeightBytes() {
        uint64_t tot = 0;
        const auto sum_table = [&](const Dictionary& T) {
            const Array kk = T.keys();
            for(int i = 0; i < kk.size(); ++i) {
                const Dictionary d = T[kk[i]];
                if(d.has(WEIGHT_KEY)) tot += uint64_t(int64_t(d[WEIGHT_KEY]));
            }
        };
        sum_table(manifestAssetsDict());
        sum_table(manifestBinDict());
        sum_table(manifestTexDict());
        return tot;
    }

    void Importer::pruneEmptyGroupsInManifest() {
        if(!m_assetsPackManifest.has(GROUPS_KEY)) return;
        const Dictionary A = manifestAssetsDict();
        const Array ks = A.keys();
        Dictionary usedGroups;
        for(int i = 0; i < ks.size(); ++i) {
            if(!A.has(ks[i])) continue;
            const Dictionary row = Dictionary(A[ks[i]]);
            if(!row.has(GROUP_KEY)) continue;
            const String g = String(row[GROUP_KEY]).strip_edges();
            if(!g.is_empty()) usedGroups[g] = true;
        }
        const Array g = manifestGroupsArray();
        Array kept;
        for(int i = 0; i < g.size(); ++i) {
            const String gv = String(g[i]).strip_edges();
            if(!gv.is_empty() && usedGroups.has(gv)) kept.append(gv);
        }
        m_assetsPackManifest[GROUPS_KEY] = kept;
    }

    const bool Importer::runFullManifestSidecarAndGltfDedupCommit(bool p_prune_empty_groups) {
        deduplicateOneSidecarTable(BIN_DATA_KEY);
        deduplicateOneSidecarTable(TEX_DATA_KEY);
        deduplicateGltfAssetsInManifest();
        return commitPackManifestToDisk(p_prune_empty_groups);
    }

    void Importer::extractBinTexMapsFromAssetRow(const Dictionary& p_row, Dictionary& p_out_bin, Dictionary& p_out_tex) {
        p_out_bin = p_row.has(BIN_ROW_KEY) && p_row[BIN_ROW_KEY].get_type() == Variant::Type::DICTIONARY
            ? Dictionary(p_row[BIN_ROW_KEY]) : Dictionary();
        p_out_tex = p_row.has(TEX_ROW_KEY) && p_row[TEX_ROW_KEY].get_type() == Variant::Type::DICTIONARY
            ? Dictionary(p_row[TEX_ROW_KEY]) : Dictionary();
    }

    const String Importer::fingerprintManifestSidecarDict(const Dictionary& p_dict) {
        Array kk = p_dict.keys();
        kk.sort();
        String acc;
        for(int i = 0; i < kk.size(); ++i)
            acc += String(kk[i]) + String("=") + String(p_dict[kk[i]]) + String(";");
        return acc;
    }

    void Importer::buildAllowedReferencedPackFilenames(Dictionary& p_out_allow) {
        p_out_allow.clear();
        const auto fill = [&](const Dictionary& T) {
            const Array kk = T.keys();
            for(int i = 0; i < kk.size(); ++i) p_out_allow[Variant(String(kk[i]))] = true;
        };
        fill(manifestAssetsDict());
        fill(manifestBinDict());
        fill(manifestTexDict());
        p_out_allow[Variant(String(M_MANIFEST_JSON_FILENAME.c_str()))] = true;
    }

    void Importer::recordGltfRowInManifest(const Dictionary& p_item) {
        const String key = p_item["pack_name"];
        Dictionary a = manifestAssetsDict();
        Dictionary row;
        row[GROUP_KEY]     = p_item["group"];
        row[TRUE_PATH_KEY] = p_item["true_path"];
        row[BIN_ROW_KEY]   = p_item.has(BIN_ROW_KEY)  ? p_item[BIN_ROW_KEY]  : Variant(Dictionary());
        row[TEX_ROW_KEY]   = p_item.has(TEX_ROW_KEY)  ? p_item[TEX_ROW_KEY]  : Variant(Dictionary());
        row[WEIGHT_KEY]    = p_item["gltf_size"];
        a[Variant(key)] = row;
        m_assetsPackManifest[ASSETS_KEY] = a;
        const String gp = p_item["group"].operator String();
        if(!gp.is_empty()) {
            Array g = manifestGroupsArray();
            bool found = false;
            for(int i = 0; i < g.size(); ++i) {
                if(String(g[i]) == gp) { found = true; break; }
            }
            if(!found) g.append(gp);
            m_assetsPackManifest[GROUPS_KEY] = g;
        }
    }

    void Importer::deduplicateOneSidecarTable(const String& p_table_name) {
        if(!m_assetsPackManifest.has(p_table_name)) return;
        Dictionary T = m_assetsPackManifest[p_table_name];
        const Array keys = T.keys();
        Dictionary groupKeyToNames;
        for(int i = 0; i < keys.size(); ++i) {
            const String pk = String(keys[i]);
            if(!T.has(Variant(pk))) continue;
            const Dictionary row = T[pk];
            if(!row.has(TRUE_PATH_KEY) || !row.has(WEIGHT_KEY)) continue;
            const String gk = String(row[TRUE_PATH_KEY]) + String("||") + String::num_int64(int64_t(row[WEIGHT_KEY]));
            Array names;
            if(groupKeyToNames.has(Variant(gk))) names = groupKeyToNames[Variant(gk)];
            bool found = false;
            for(int vi = 0; vi < names.size(); ++vi) {
                if(names[vi] == Variant(pk)) { found = true; break; }
            }
            if(!found) names.append(pk);
            groupKeyToNames[Variant(gk)] = names;
        }
        const Array gks = groupKeyToNames.keys();
        for(int gi = 0; gi < gks.size(); ++gi) {
            Array nms = groupKeyToNames[gks[gi]];
            if(nms.size() < 2) continue;
            const String canon = String(nms[0]);
            int j = 1;
            while(j < nms.size()) {
                const String dup = String(nms[j]);
                Dictionary Tnow = Dictionary(m_assetsPackManifest[p_table_name]);
                if(!Tnow.has(Variant(canon)) || !Tnow.has(Variant(dup))) { nms.remove_at(j); continue; }
                if(!fileBinaryEqual(m_assetPackPath.path_join(canon), m_assetPackPath.path_join(dup))) { ++j; continue; }
                mergeRepairPackSidecarsInTable(p_table_name, canon, dup, false);
                nms.remove_at(j);
            }
        }
    }

    void Importer::deduplicateGltfAssetsInManifest() {
        if(!m_assetsPackManifest.has(ASSETS_KEY)) return;
        bool progress = true;
        while(progress) {
            progress = false;
            Dictionary A   = Dictionary(m_assetsPackManifest[ASSETS_KEY]);
            const Array ak = A.keys();
            Dictionary metaKeys;
            for(int i = 0; i < ak.size(); ++i) {
                const String nm = String(ak[i]);
                if(!A.has(Variant(nm))) continue;
                const Dictionary da = Dictionary(A[nm]);
                Dictionary dab, dat;
                extractBinTexMapsFromAssetRow(da, dab, dat);
                const String tp = da.has(TRUE_PATH_KEY) ? String(da[TRUE_PATH_KEY]) : String();
                metaKeys[Variant(nm)] = tp + String(";") + fingerprintManifestSidecarDict(dab) + String(";")
                    + fingerprintManifestSidecarDict(dat);
            }
            bool merged = false;
            for(int i = 0; i < ak.size() - 1 && !merged; ++i) {
                const String rowA = String(ak[i]);
                if(!A.has(Variant(rowA))) continue;
                const String metaA = String(metaKeys[rowA]);
                for(int jj = i + 1; jj < ak.size() && !merged; ++jj) {
                    const String kb = String(ak[jj]);
                    if(!A.has(Variant(kb))) continue;
                    if(String(metaKeys[kb]) != metaA) continue;
                    if(mergeRepairDuplicateGltfPackFiles(rowA, kb, false, false)) {
                        merged = true; progress = true;
                    }
                }
            }
        }
    }

}
