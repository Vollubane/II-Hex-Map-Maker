#include "Importer.h"
#include "Importer_Constants.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace ImportExportModule {

    void Importer::progressImportCopyStateForFrame() {
        if(m_copy.subPhase == E_CopySubPhase::Gltf) {
            while(0.0 < m_timeBudget && 0 < m_copy.plannedGltf.size()) {
                const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
                const Dictionary item = m_copy.plannedGltf[0];
                m_copy.plannedGltf.remove_at(0);
                if(!copyGltfPlannedItem(item)) { m_importerState = E_ImporterState::Destruct; return; }
                debitTimeBudgetFromTicks(t0);
            }
            if(0 < m_copy.plannedGltf.size()) return;
            m_copy.subPhase = E_CopySubPhase::Sidecar;
        }
        if(m_copy.subPhase == E_CopySubPhase::Sidecar) {
            if(!runSidecarPhaseSlice()) { m_importerState = E_ImporterState::Destruct; return; }
            if(m_copy.subPhase == E_CopySubPhase::Sidecar) return;
        }
        if(m_copy.subPhase == E_CopySubPhase::Dedup) {
            if(!runDedupAndFinish()) m_importerState = E_ImporterState::Destruct;
        }
    }

    const bool Importer::enumerateGltfUnderImportPath(const String& p_import_path, Array& p_gltf_list, int p_current_deep) {
        if(p_current_deep > FILE_MANIPULATION_MAX_DEEP) return false;
        if(FileAccess::file_exists(p_import_path)) {
            if(p_import_path.to_lower().ends_with(".gltf")) p_gltf_list.append(p_import_path);
            return true;
        }
        const Ref<DirAccess> dir = DirAccess::open(p_import_path);
        if(dir.is_null()) return false;
        dir->list_dir_begin();
        String cur = dir->get_next();
        while(cur != String()) {
            if(cur != "." && cur != "..") {
                const String child = p_import_path.path_join(cur);
                if(dir->current_is_dir()) {
                    if(!enumerateGltfUnderImportPath(child, p_gltf_list, p_current_deep + 1)) {
                        dir->list_dir_end();
                        return false;
                    }
                } else if(cur.to_lower().ends_with(".gltf")) {
                    p_gltf_list.append(child);
                }
            }
            cur = dir->get_next();
        }
        dir->list_dir_end();
        return true;
    }

    const bool Importer::buildImportPlan(const Array& p_gltf_source_list) {
        Dictionary reserved;
        collectReservedNamesFromPack(reserved);
        m_copy.plannedGltf.clear();
        m_copy.plannedSidecar.clear();
        m_copy.newBytes = 0;
        Dictionary sidecarBySource;
        for(int g = 0; g < p_gltf_source_list.size(); ++g) {
            if(p_gltf_source_list[g].get_type() != Variant::Type::STRING) return false;
            const String gsrc = p_gltf_source_list[g];
            const Ref<FileAccess> gf = FileAccess::open(gsrc, FileAccess::ModeFlags::READ);
            if(gf.is_null()) return false;
            const int64_t glen = int64_t(gf->get_length());
            m_copy.newBytes += uint64_t(glen);
            Ref<JSON> j;
            j.instantiate();
            if(j->parse(gf->get_as_text()) != OK) {
                UtilityFunctions::push_error(String("Importer: plan, invalid JSON: ") + gsrc);
                return false;
            }
            const Variant vr = j->get_data();
            if(vr.get_type() != Variant::Type::DICTIONARY) return false;
            const Dictionary root = vr;
            Array ulist;
            appendGltfBuffersAndImagesUriRows(root, ulist);
            for(int uu = 0; uu < ulist.size(); ++uu) {
                if(!planOneSidecar(gsrc, ulist[uu], sidecarBySource, reserved)) return false;
            }
            const String packG  = makeUniqueNameInSet(gsrc.get_file(), reserved);
            const String tpath  = toImportRootRelativePath(gsrc);
            const String ggroup = groupPathForSourceGltf(gsrc);
            Dictionary uriMap, binD, texD;
            for(int uu = 0; uu < ulist.size(); ++uu) {
                const Dictionary um  = ulist[uu];
                const String uri     = String(um["uri"]);
                const String absR    = resolveGltfUriToAbsoluteFile(gsrc, uri);
                if(absR.is_empty()) continue;
                const String skey = absR.simplify_path();
                if(!sidecarBySource.has(Variant(skey))) continue;
                const String newName = String(sidecarBySource[Variant(skey)]);
                uriMap[Variant(uri)] = newName;
                if(bool(um["is_buffer"])) binD[Variant(newName)] = toImportRootRelativePath(skey);
                else                      texD[Variant(newName)] = toImportRootRelativePath(skey);
            }
            Dictionary item;
            item["source"]      = gsrc;
            item["pack_name"]   = packG;
            item["true_path"]   = tpath;
            item["group"]       = ggroup;
            item["gltf_size"]   = glen;
            item["uri_to_pack"] = uriMap;
            item[BIN_ROW_KEY]   = binD;
            item[TEX_ROW_KEY]   = texD;
            m_copy.plannedGltf.append(item);
        }
        if(DirAccess::open(m_assetPackPath).is_null()) return false;
        if(!checkDiskSpace(m_copy.newBytes)) {
            UtilityFunctions::push_error("Importer: plan, not enough disk space for the whole import");
            m_copy.plannedGltf.clear();
            m_copy.plannedSidecar.clear();
            return false;
        }
        return !m_copy.plannedGltf.is_empty();
    }

    const bool Importer::planOneSidecar(const String& p_gltf_src, const Dictionary& p_uri_row,
        Dictionary& p_src_to_pack, Dictionary& p_reserved) {
        const String uri  = String(p_uri_row["uri"]);
        const bool isBin  = bool(p_uri_row["is_buffer"]);
        const String abs  = resolveGltfUriToAbsoluteFile(p_gltf_src, uri);
        if(abs.is_empty() || !FileAccess::file_exists(abs)) return true;
        const String simp = abs.simplify_path();
        if(p_src_to_pack.has(Variant(simp))) return true;
        const String tRel = toImportRootRelativePath(simp);
        const Ref<FileAccess> rf = FileAccess::open(simp, FileAccess::ModeFlags::READ);
        if(rf.is_null()) return true;
        const uint64_t sz = rf->get_length();
        String reuse;
        if(fileExistsInPackByTruePath(tRel, sz, isBin, reuse)) {
            p_src_to_pack[Variant(simp)] = reuse;
            return true;
        }
        const String packName = pickPackNameForNewSidecar(simp, isBin, sz, p_reserved);
        p_src_to_pack[Variant(simp)] = packName;
        Dictionary en;
        en["source"]    = simp;
        en["pack_name"] = packName;
        en["true_path"] = tRel;
        en["is_bin"]    = isBin;
        en["size"]      = int64_t(sz);
        m_copy.plannedSidecar.append(en);
        m_copy.newBytes += sz;
        return true;
    }

    const bool Importer::checkDiskSpace(uint64_t p_need_bytes) {
        const Ref<DirAccess> d = DirAccess::open(m_assetPackPath);
        if(d.is_null()) return false;
        const uint64_t sp = d->get_space_left();
        if(0 < sp && p_need_bytes + (1024ull * 1024) > sp) return p_need_bytes < sp;
        if(0 < sp) return p_need_bytes <= sp;
        return true;
    }

    const bool Importer::copyGltfPlannedItem(const Dictionary& p_item) {
        const String src  = p_item["source"];
        const String pnm  = p_item["pack_name"];
        const String dest = m_assetPackPath.path_join(pnm);
        if(DirAccess::copy_absolute(src, dest) != OK) {
            UtilityFunctions::push_error(String("Importer: copy gltf failed: ") + src);
            return false;
        }
        const Ref<FileAccess> rf = FileAccess::open(dest, FileAccess::ModeFlags::READ);
        if(rf.is_null()) return false;
        String out = rf->get_as_text();
        sortDedupAndApplyUriStrings(out, p_item["uri_to_pack"]);
        const Ref<FileAccess> wf = FileAccess::open(dest, FileAccess::ModeFlags::WRITE);
        if(wf.is_null()) return false;
        wf->store_string(out);
        recordGltfRowInManifest(p_item);
        m_picturing.toCapture.append(dest);
        return true;
    }

    const bool Importer::copySidecarToPackAndRecord(const String& p_source, const String& p_pack_name,
        const String& p_true_path, bool p_is_bin, int64_t p_size) {
        const String dest = m_assetPackPath.path_join(p_pack_name);
        if(DirAccess::copy_absolute(p_source, dest) != OK) {
            UtilityFunctions::push_error(String("Importer: sidecar copy failed: ") + p_source);
            return false;
        }
        const char* const tname = p_is_bin ? BIN_DATA_KEY : TEX_DATA_KEY;
        Dictionary T = m_assetsPackManifest.has(tname) ? Dictionary(m_assetsPackManifest[tname]) : Dictionary();
        T[Variant(p_pack_name)] = makeSidecarRow(p_true_path, p_size);
        m_assetsPackManifest[tname] = T;
        return true;
    }

    const bool Importer::runSidecarPhaseSlice() {
        if(m_copy.plannedSidecar.is_empty()) {
            m_copy.subPhase = E_CopySubPhase::Dedup;
            return true;
        }
        while(0.0 < m_timeBudget && 0 < m_copy.plannedSidecar.size()) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const Dictionary d = m_copy.plannedSidecar[0];
            m_copy.plannedSidecar.remove_at(0);
            if(!copySidecarToPackAndRecord(
                    String(d["source"]), String(d["pack_name"]),
                    String(d["true_path"]), bool(d["is_bin"]), int64_t(d["size"]))) {
                return false;
            }
            debitTimeBudgetFromTicks(t0);
        }
        if(0 < m_copy.plannedSidecar.size()) return true;
        m_copy.subPhase = E_CopySubPhase::Dedup;
        return true;
    }

    const bool Importer::runDedupAndFinish() {
        if(!runFullManifestSidecarAndGltfDedupCommit(true)) return false;
        m_importerState = E_ImporterState::Picturing;
        return true;
    }

    const bool Importer::pathIsUnderPack(const String& p_abs_path) {
        const String p = m_assetPackPath.simplify_path();
        const String a = p_abs_path.simplify_path();
        return (a == p) || a.begins_with(p + String("/"));
    }

    const String Importer::toImportRootRelativePath(const String& p_abs) {
        const String root = m_importSourceRoot.simplify_path();
        const String abs  = p_abs.simplify_path();
        if(abs == root) return String();
        if(abs.begins_with(root + String("/")))
            return abs.substr(root.length()).trim_prefix("/").replace("\\", "/");
        return p_abs.get_file();
    }

    const String Importer::groupPathForSourceGltf(const String& p_src_gltf_path) {
        const String root = m_importSourceRoot.simplify_path();
        const String abs  = p_src_gltf_path.simplify_path();
        String relative   = abs.get_file();
        if(abs == root || abs.begins_with(root + String("/")))
            relative = abs == root ? String() : abs.substr(root.length()).trim_prefix("/").replace("\\", "/");
        const String dir = relative.get_base_dir();
        return (dir == "." || dir.is_empty()) ? String("") : dir;
    }

    const String Importer::makeUniqueNameInSet(const String& p_preferred, Dictionary& p_reserved) {
        if(!p_preferred.is_empty() && !p_reserved.has(Variant(p_preferred))) {
            p_reserved[Variant(p_preferred)] = true;
            return p_preferred;
        }
        const String ext  = p_preferred.get_extension();
        const String stem = p_preferred.get_basename();
        int d = 2;
        for(;;) {
            const String c = stem + String("_") + String::num_int64(d) + (ext.is_empty() ? String() : (String(".") + ext));
            ++d;
            if(10000 < d) return p_preferred;
            if(!p_reserved.has(Variant(c))) { p_reserved[Variant(c)] = true; return c; }
        }
    }

    const String Importer::pickPackNameForNewSidecar(const String& p_source_abs, bool p_is_bin, uint64_t p_size, Dictionary& p_reserved) {
        (void)p_size;
        const String fileOnly = p_source_abs.get_file();
        const String ext      = fileOnly.get_extension();
        const String base     = p_is_bin && ext.to_lower() == "bin" ? fileOnly
                            : p_is_bin ? (fileOnly.get_basename() + String(".bin"))
                                        : fileOnly;
        return makeUniqueNameInSet(base, p_reserved);
    }

    const bool Importer::fileExistsInPackByTruePath(const String& p_true_path_rel, uint64_t p_size, bool p_is_bin, String& p_out_name) {
        const char* const tname = p_is_bin ? BIN_DATA_KEY : TEX_DATA_KEY;
        if(!m_assetsPackManifest.has(tname)) return false;
        const Dictionary t = m_assetsPackManifest[tname];
        const Array keys   = t.keys();
        for(int i = 0; i < keys.size(); ++i) {
            const String key = String(keys[i]);
            if(!t.has(Variant(key))) continue;
            const Dictionary row = t[key];
            if(!row.has(TRUE_PATH_KEY) || !row.has(WEIGHT_KEY)) continue;
            if(String(row[TRUE_PATH_KEY]) != p_true_path_rel) continue;
            if(uint64_t(int64_t(row[WEIGHT_KEY])) != p_size) continue;
            p_out_name = key;
            return true;
        }
        return false;
    }

    void Importer::collectReservedNamesFromPack(Dictionary& p_reserved) {
        const auto reserveTable = [&](const Dictionary& tab) {
            const Array kk = tab.keys();
            for(int i = 0; i < kk.size(); ++i) p_reserved[Variant(String(kk[i]))] = true;
        };
        reserveTable(manifestAssetsDict());
        reserveTable(manifestBinDict());
        reserveTable(manifestTexDict());
        if(!m_assetPackPath.is_empty()) {
            Array rootNames;
            enumerateFilesInDirNonRecursive(m_assetPackPath, rootNames);
            for(int i = 0; i < rootNames.size(); ++i) p_reserved[Variant(String(rootNames[i]))] = true;
        }
    }

    void Importer::appendGltfBuffersAndImagesUriRows(const Dictionary& p_root, Array& p_out_uri_rows) {
        const auto appendSection = [&](const char* section, bool isBuffer) {
            if(!p_root.has(section) || p_root[section].get_type() != Variant::Type::ARRAY) return;
            const Array arr = p_root[section];
            for(int i = 0; i < arr.size(); ++i) {
                if(arr[i].get_type() != Variant::Type::DICTIONARY) continue;
                const Dictionary entry = arr[i];
                if(!entry.has("uri")) continue;
                const String u = String(entry["uri"]);
                if(gltfUriCannotMapToLocalFile(u)) continue;
                Dictionary emitted;
                emitted["uri"]       = u;
                emitted["is_buffer"] = isBuffer;
                p_out_uri_rows.append(emitted);
            }
        };
        appendSection("buffers", true);
        appendSection("images",  false);
    }

    const bool Importer::gltfUriCannotMapToLocalFile(const String& p_uri) {
        return p_uri.is_empty() || p_uri.begins_with("data:") || p_uri.begins_with("file://")
            || (0 <= p_uri.find("://") && !p_uri.begins_with("file://"));
    }

    const String Importer::resolveGltfUriToAbsoluteFile(const String& p_src_gltf_path, const String& p_uri) {
        if(gltfUriCannotMapToLocalFile(p_uri)) return {};
        const String base = p_src_gltf_path.get_base_dir();
        if(p_uri.is_relative_path()) return base.path_join(p_uri).simplify_path();
        return p_uri.simplify_path();
    }

    void Importer::sortDedupAndApplyUriStrings(String& p_text, const Dictionary& p_uri_to_name) {
        Array pairs;
        const Array ks = p_uri_to_name.keys();
        for(int i = 0; i < ks.size(); ++i) {
            const String oldU = String(ks[i]);
            const String newF = String(p_uri_to_name[Variant(oldU)]);
            if(oldU == newF) continue;
            Array p; p.append(oldU); p.append(newF);
            pairs.append(p);
        }
        {
            Dictionary seen;
            Array out;
            for(int i = 0; i < pairs.size(); ++i) {
                const Array pr = pairs[i];
                if(pr.size() < 2) continue;
                const String o = String(pr[0]);
                if(seen.has(Variant(o))) continue;
                seen[Variant(o)] = true;
                out.append(pr);
            }
            pairs = out;
        }
        const int n = int(pairs.size());
        for(int a = 0; a < n - 1; ++a) {
            for(int b = a + 1; b < n; ++b) {
                if(String(Array(pairs[b])[0]).length() > String(Array(pairs[a])[0]).length()) {
                    const Variant tmp = pairs[a]; pairs[a] = pairs[b]; pairs[b] = tmp;
                }
            }
        }
        for(int i = 0; i < pairs.size(); ++i) {
            const Array pr = pairs[i];
            if(pr.size() < 2) continue;
            const String o  = String(pr[0]);
            const String nw = String(pr[1]);
            if(o == nw) continue;
            const String oq = JSON::stringify(Variant(o),  String(), false, true);
            const String nq = JSON::stringify(Variant(nw), String(), false, true);
            if(p_text.find(oq) < 0) {
                UtilityFunctions::push_warning(String("Importer: gltf uri not found in file text, skipped: ") + o);
                continue;
            }
            p_text = p_text.replace(oq, nq);
        }
    }

}
