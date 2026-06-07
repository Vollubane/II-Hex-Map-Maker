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
        if(m_copy.subPhase == E_CopySubPhase::Gltf)
            _emitLoading("Importing", "Copying glTF files",
                String::num_int64(m_copy.plannedGltf.size()) + String(" left"));
        else if(m_copy.subPhase == E_CopySubPhase::Sidecar)
            _emitLoading("Importing", "Copying sidecar files",
                String::num_int64(m_copy.plannedSidecar.size()) + String(" left"));
        else if(m_copy.subPhase == E_CopySubPhase::Dedup)
            _emitLoading("Importing", "Deduplicating", "");

        if(m_copy.subPhase == E_CopySubPhase::Gltf) {
            while(0.0 < m_timeBudget && 0 < m_copy.plannedGltf.size()) {
                const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
                const Dictionary gltfPlannedItem = m_copy.plannedGltf[0];
                m_copy.plannedGltf.remove_at(0);
                if(!copyGltfPlannedItem(gltfPlannedItem)) { m_importerState = E_ImporterState::Destruct; return; }
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
        String currentEntry = dir->get_next();
        while(currentEntry != String()) {
            if(currentEntry != "." && currentEntry != "..") {
                const String childPath = p_import_path.path_join(currentEntry);
                if(dir->current_is_dir()) {
                    if(!enumerateGltfUnderImportPath(childPath, p_gltf_list, p_current_deep + 1)) {
                        dir->list_dir_end();
                        return false;
                    }
                } else if(currentEntry.to_lower().ends_with(".gltf")) {
                    p_gltf_list.append(childPath);
                }
            }
            currentEntry = dir->get_next();
        }
        dir->list_dir_end();
        return true;
    }

    const bool Importer::buildImportPlan(const Array& p_gltf_source_list) {
        Dictionary reservedFilenames;
        collectReservedNamesFromPack(reservedFilenames);
        m_copy.plannedGltf.clear();
        m_copy.plannedSidecar.clear();
        m_copy.newBytes = 0;
        Dictionary sidecarBySourcePath;
        for(int g = 0; g < p_gltf_source_list.size(); ++g) {
            if(p_gltf_source_list[g].get_type() != Variant::Type::STRING) return false;
            const String gltfSourcePath = p_gltf_source_list[g];
            const Ref<FileAccess> gltfSourceFile = FileAccess::open(gltfSourcePath, FileAccess::ModeFlags::READ);
            if(gltfSourceFile.is_null()) return false;
            const int64_t gltfFileSizeBytes = int64_t(gltfSourceFile->get_length());
            m_copy.newBytes += uint64_t(gltfFileSizeBytes);
            Ref<JSON> jsonParser;
            jsonParser.instantiate();
            if(jsonParser->parse(gltfSourceFile->get_as_text()) != OK) {
                UtilityFunctions::push_error(String("Importer: plan, invalid JSON: ") + gltfSourcePath);
                return false;
            }
            const Variant parsedJson = jsonParser->get_data();
            if(parsedJson.get_type() != Variant::Type::DICTIONARY) return false;
            const Dictionary gltfRoot = parsedJson;
            Array uriRows;
            appendGltfBuffersAndImagesUriRows(gltfRoot, uriRows);
            for(int ui = 0; ui < uriRows.size(); ++ui) {
                if(!planOneSidecar(gltfSourcePath, uriRows[ui], sidecarBySourcePath, reservedFilenames)) return false;
            }
            const String packFilename       = makeUniqueNameInSet(gltfSourcePath.get_file(), reservedFilenames);
            const String importRelativePath = toImportRootRelativePath(gltfSourcePath);
            const String groupSubdir        = groupPathForSourceGltf(gltfSourcePath);
            Dictionary uriToPackName, binSidecarEntries, texSidecarEntries;
            for(int ui = 0; ui < uriRows.size(); ++ui) {
                const Dictionary uriRow        = uriRows[ui];
                const String originalUri       = String(uriRow["uri"]);
                const String absoluteFilePath  = resolveGltfUriToAbsoluteFile(gltfSourcePath, originalUri);
                if(absoluteFilePath.is_empty()) continue;
                const String simplifiedSourcePath = absoluteFilePath.simplify_path();
                if(!sidecarBySourcePath.has(Variant(simplifiedSourcePath))) continue;
                const String assignedPackName = String(sidecarBySourcePath[Variant(simplifiedSourcePath)]);
                uriToPackName[Variant(originalUri)] = assignedPackName;
                if(bool(uriRow["is_buffer"])) binSidecarEntries[Variant(assignedPackName)] = toImportRootRelativePath(simplifiedSourcePath);
                else                          texSidecarEntries[Variant(assignedPackName)] = toImportRootRelativePath(simplifiedSourcePath);
            }
            Dictionary gltfPlannedItem;
            gltfPlannedItem["source"]      = gltfSourcePath;
            gltfPlannedItem["pack_name"]   = packFilename;
            gltfPlannedItem["true_path"]   = importRelativePath;
            gltfPlannedItem["group"]       = groupSubdir;
            gltfPlannedItem["gltf_size"]   = gltfFileSizeBytes;
            gltfPlannedItem["uri_to_pack"] = uriToPackName;
            gltfPlannedItem[BIN_ROW_KEY]   = binSidecarEntries;
            gltfPlannedItem[TEX_ROW_KEY]   = texSidecarEntries;
            m_copy.plannedGltf.append(gltfPlannedItem);
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
        const String originalUri      = String(p_uri_row["uri"]);
        const bool isBin              = bool(p_uri_row["is_buffer"]);
        const String absoluteFilePath = resolveGltfUriToAbsoluteFile(p_gltf_src, originalUri);
        if(absoluteFilePath.is_empty() || !FileAccess::file_exists(absoluteFilePath)) return true;
        const String simplifiedSourcePath = absoluteFilePath.simplify_path();
        if(p_src_to_pack.has(Variant(simplifiedSourcePath))) return true;
        const String importRelativePath = toImportRootRelativePath(simplifiedSourcePath);
        const Ref<FileAccess> sidecarFile = FileAccess::open(simplifiedSourcePath, FileAccess::ModeFlags::READ);
        if(sidecarFile.is_null()) return true;
        const uint64_t fileSizeBytes = sidecarFile->get_length();
        String existingPackName;
        if(fileExistsInPackByTruePath(importRelativePath, fileSizeBytes, isBin, existingPackName)) {
            p_src_to_pack[Variant(simplifiedSourcePath)] = existingPackName;
            return true;
        }
        const String newPackFilename = pickPackNameForNewSidecar(simplifiedSourcePath, isBin, fileSizeBytes, p_reserved);
        p_src_to_pack[Variant(simplifiedSourcePath)] = newPackFilename;
        Dictionary sidecarPlan;
        sidecarPlan["source"]    = simplifiedSourcePath;
        sidecarPlan["pack_name"] = newPackFilename;
        sidecarPlan["true_path"] = importRelativePath;
        sidecarPlan["is_bin"]    = isBin;
        sidecarPlan["size"]      = int64_t(fileSizeBytes);
        m_copy.plannedSidecar.append(sidecarPlan);
        m_copy.newBytes += fileSizeBytes;
        return true;
    }

    const bool Importer::checkDiskSpace(uint64_t p_need_bytes) {
        const Ref<DirAccess> packDir = DirAccess::open(m_assetPackPath);
        if(packDir.is_null()) return false;
        const uint64_t availableSpaceBytes = packDir->get_space_left();
        if(0 < availableSpaceBytes && p_need_bytes + (1024ull * 1024) > availableSpaceBytes) return p_need_bytes < availableSpaceBytes;
        if(0 < availableSpaceBytes) return p_need_bytes <= availableSpaceBytes;
        return true;
    }

    const bool Importer::copyGltfPlannedItem(const Dictionary& p_item) {
        const String sourcePath    = p_item["source"];
        const String packFilename  = p_item["pack_name"];
        const String destAbsPath   = m_assetPackPath.path_join(packFilename);
        if(DirAccess::copy_absolute(sourcePath, destAbsPath) != OK) {
            UtilityFunctions::push_error(String("Importer: copy gltf failed: ") + sourcePath);
            return false;
        }
        const Ref<FileAccess> copiedFile = FileAccess::open(destAbsPath, FileAccess::ModeFlags::READ);
        if(copiedFile.is_null()) return false;
        String gltfFileContent = copiedFile->get_as_text();
        sortDedupAndApplyUriStrings(gltfFileContent, p_item["uri_to_pack"]);
        const Ref<FileAccess> writeFile = FileAccess::open(destAbsPath, FileAccess::ModeFlags::WRITE);
        if(writeFile.is_null()) return false;
        writeFile->store_string(gltfFileContent);
        recordGltfRowInManifest(p_item);
        m_picturing.toCapture.append(destAbsPath);
        return true;
    }

    const bool Importer::copySidecarToPackAndRecord(const String& p_source, const String& p_pack_name,
        const String& p_true_path, bool p_is_bin, int64_t p_size) {
        const String destAbsPath = m_assetPackPath.path_join(p_pack_name);
        if(DirAccess::copy_absolute(p_source, destAbsPath) != OK) {
            UtilityFunctions::push_error(String("Importer: sidecar copy failed: ") + p_source);
            return false;
        }
        const char* const sidecarTableName = p_is_bin ? BIN_DATA_KEY : TEX_DATA_KEY;
        Dictionary sidecarTable = m_assetsPackManifest.has(sidecarTableName) ? Dictionary(m_assetsPackManifest[sidecarTableName]) : Dictionary();
        sidecarTable[Variant(p_pack_name)] = makeSidecarRow(p_true_path, p_size);
        m_assetsPackManifest[sidecarTableName] = sidecarTable;
        return true;
    }

    const bool Importer::runSidecarPhaseSlice() {
        if(m_copy.plannedSidecar.is_empty()) {
            m_copy.subPhase = E_CopySubPhase::Dedup;
            return true;
        }
        while(0.0 < m_timeBudget && 0 < m_copy.plannedSidecar.size()) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const Dictionary sidecarItem = m_copy.plannedSidecar[0];
            m_copy.plannedSidecar.remove_at(0);
            if(!copySidecarToPackAndRecord(
                    String(sidecarItem["source"]), String(sidecarItem["pack_name"]),
                    String(sidecarItem["true_path"]), bool(sidecarItem["is_bin"]), int64_t(sidecarItem["size"]))) {
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
        const String normalizedPackPath = m_assetPackPath.simplify_path();
        const String normalizedAbsPath  = p_abs_path.simplify_path();
        return (normalizedAbsPath == normalizedPackPath) || normalizedAbsPath.begins_with(normalizedPackPath + String("/"));
    }

    const String Importer::toImportRootRelativePath(const String& p_abs) {
        const String normalizedImportRoot = m_importSourceRoot.simplify_path();
        const String normalizedAbsPath    = p_abs.simplify_path();
        if(normalizedAbsPath == normalizedImportRoot) return String();
        if(normalizedAbsPath.begins_with(normalizedImportRoot + String("/")))
            return normalizedAbsPath.substr(normalizedImportRoot.length()).trim_prefix("/").replace("\\", "/");
        return p_abs.get_file();
    }

    const String Importer::groupPathForSourceGltf(const String& p_src_gltf_path) {
        const String normalizedImportRoot = m_importSourceRoot.simplify_path();
        const String normalizedAbsPath    = p_src_gltf_path.simplify_path();
        String relativePath               = normalizedAbsPath.get_file();
        if(normalizedAbsPath == normalizedImportRoot || normalizedAbsPath.begins_with(normalizedImportRoot + String("/")))
            relativePath = normalizedAbsPath == normalizedImportRoot ? String()
                : normalizedAbsPath.substr(normalizedImportRoot.length()).trim_prefix("/").replace("\\", "/");
        const String parentDirectory = relativePath.get_base_dir();
        return (parentDirectory == "." || parentDirectory.is_empty()) ? String("") : parentDirectory;
    }

    const String Importer::makeUniqueNameInSet(const String& p_preferred, Dictionary& p_reserved) {
        if(!p_preferred.is_empty() && !p_reserved.has(Variant(p_preferred))) {
            p_reserved[Variant(p_preferred)] = true;
            return p_preferred;
        }
        const String ext  = p_preferred.get_extension();
        const String stem = p_preferred.get_basename();
        int counter = 2;
        for(;;) {
            const String candidateName = stem + String("_") + String::num_int64(counter) + (ext.is_empty() ? String() : (String(".") + ext));
            ++counter;
            if(10000 < counter) return p_preferred;
            if(!p_reserved.has(Variant(candidateName))) { p_reserved[Variant(candidateName)] = true; return candidateName; }
        }
    }

    const String Importer::pickPackNameForNewSidecar(const String& p_source_abs, bool p_is_bin, uint64_t p_size, Dictionary& p_reserved) {
        (void)p_size;
        const String sourceFilename = p_source_abs.get_file();
        const String ext            = sourceFilename.get_extension();
        const String basePackName   = p_is_bin && ext.to_lower() == "bin" ? sourceFilename
                                    : p_is_bin ? (sourceFilename.get_basename() + String(".bin"))
                                               : sourceFilename;
        return makeUniqueNameInSet(basePackName, p_reserved);
    }

    const bool Importer::fileExistsInPackByTruePath(const String& p_true_path_rel, uint64_t p_size, bool p_is_bin, String& p_out_name) {
        const char* const sidecarTableName = p_is_bin ? BIN_DATA_KEY : TEX_DATA_KEY;
        if(!m_assetsPackManifest.has(sidecarTableName)) return false;
        const Dictionary sidecarTable = m_assetsPackManifest[sidecarTableName];
        const Array sidecarFilenames  = sidecarTable.keys();
        for(int i = 0; i < sidecarFilenames.size(); ++i) {
            const String sidecarFilename = String(sidecarFilenames[i]);
            if(!sidecarTable.has(Variant(sidecarFilename))) continue;
            const Dictionary sidecarRow = sidecarTable[sidecarFilename];
            if(!sidecarRow.has(TRUE_PATH_KEY) || !sidecarRow.has(WEIGHT_KEY)) continue;
            if(String(sidecarRow[TRUE_PATH_KEY]) != p_true_path_rel) continue;
            if(uint64_t(int64_t(sidecarRow[WEIGHT_KEY])) != p_size) continue;
            p_out_name = sidecarFilename;
            return true;
        }
        return false;
    }

    void Importer::collectReservedNamesFromPack(Dictionary& p_reserved) {
        const auto reserveAllKeysInTable = [&](const Dictionary& manifestTable) {
            const Array tableKeys = manifestTable.keys();
            for(int i = 0; i < tableKeys.size(); ++i) p_reserved[Variant(String(tableKeys[i]))] = true;
        };
        reserveAllKeysInTable(manifestAssetsDict());
        reserveAllKeysInTable(manifestBinDict());
        reserveAllKeysInTable(manifestTexDict());
        if(!m_assetPackPath.is_empty()) {
            Array diskFilenames;
            enumerateFilesInDirNonRecursive(m_assetPackPath, diskFilenames);
            for(int i = 0; i < diskFilenames.size(); ++i) p_reserved[Variant(String(diskFilenames[i]))] = true;
        }
    }

    void Importer::appendGltfBuffersAndImagesUriRows(const Dictionary& p_root, Array& p_out_uri_rows) {
        const auto appendUriRowsFromSection = [&](const char* sectionName, bool isBuffer) {
            if(!p_root.has(sectionName) || p_root[sectionName].get_type() != Variant::Type::ARRAY) return;
            const Array sectionElements = p_root[sectionName];
            for(int i = 0; i < sectionElements.size(); ++i) {
                if(sectionElements[i].get_type() != Variant::Type::DICTIONARY) continue;
                const Dictionary sectionItem = sectionElements[i];
                if(!sectionItem.has("uri")) continue;
                const String uriString = String(sectionItem["uri"]);
                if(gltfUriCannotMapToLocalFile(uriString)) continue;
                Dictionary uriRow;
                uriRow["uri"]       = uriString;
                uriRow["is_buffer"] = isBuffer;
                p_out_uri_rows.append(uriRow);
            }
        };
        appendUriRowsFromSection("buffers", true);
        appendUriRowsFromSection("images",  false);
    }

    const bool Importer::gltfUriCannotMapToLocalFile(const String& p_uri) {
        return p_uri.is_empty() || p_uri.begins_with("data:") || p_uri.begins_with("file://")
            || (0 <= p_uri.find("://") && !p_uri.begins_with("file://"));
    }

    const String Importer::resolveGltfUriToAbsoluteFile(const String& p_src_gltf_path, const String& p_uri) {
        if(gltfUriCannotMapToLocalFile(p_uri)) return {};
        const String gltfDirectory = p_src_gltf_path.get_base_dir();
        if(p_uri.is_relative_path()) return gltfDirectory.path_join(p_uri).simplify_path();
        return p_uri.simplify_path();
    }

    void Importer::sortDedupAndApplyUriStrings(String& p_text, const Dictionary& p_uri_to_name) {
        Array uriReplacementPairs;
        const Array uriOriginals = p_uri_to_name.keys();
        for(int i = 0; i < uriOriginals.size(); ++i) {
            const String originalUri    = String(uriOriginals[i]);
            const String newPackFilename = String(p_uri_to_name[Variant(originalUri)]);
            if(originalUri == newPackFilename) continue;
            Array pair; pair.append(originalUri); pair.append(newPackFilename);
            uriReplacementPairs.append(pair);
        }
        {
            Dictionary processedOriginalUris;
            Array deduplicatedPairs;
            for(int i = 0; i < uriReplacementPairs.size(); ++i) {
                const Array pairEntry  = uriReplacementPairs[i];
                if(pairEntry.size() < 2) continue;
                const String originalUri = String(pairEntry[0]);
                if(processedOriginalUris.has(Variant(originalUri))) continue;
                processedOriginalUris[Variant(originalUri)] = true;
                deduplicatedPairs.append(pairEntry);
            }
            uriReplacementPairs = deduplicatedPairs;
        }
        const int pairCount = int(uriReplacementPairs.size());
        for(int a = 0; a < pairCount - 1; ++a) {
            for(int b = a + 1; b < pairCount; ++b) {
                if(String(Array(uriReplacementPairs[b])[0]).length() > String(Array(uriReplacementPairs[a])[0]).length()) {
                    const Variant tmp = uriReplacementPairs[a]; uriReplacementPairs[a] = uriReplacementPairs[b]; uriReplacementPairs[b] = tmp;
                }
            }
        }
        for(int i = 0; i < uriReplacementPairs.size(); ++i) {
            const Array pair = uriReplacementPairs[i];
            if(pair.size() < 2) continue;
            const String originalUri     = String(pair[0]);
            const String newPackFilename = String(pair[1]);
            if(originalUri == newPackFilename) continue;
            const String originalUriQuoted  = JSON::stringify(Variant(originalUri),     String(), false, true);
            const String newFilenameQuoted  = JSON::stringify(Variant(newPackFilename), String(), false, true);
            if(p_text.find(originalUriQuoted) < 0) {
                UtilityFunctions::push_warning(String("Importer: gltf uri not found in file text, skipped: ") + originalUri);
                continue;
            }
            p_text = p_text.replace(originalUriQuoted, newFilenameQuoted);
        }
    }

}
