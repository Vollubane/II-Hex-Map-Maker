#include "Importer.h"
#include "Importer_Constants.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace ImportExportModule {

    const Dictionary Importer::manifestAssetsDict() {
        const Variant value = m_assetsPackManifest.has(ASSETS_KEY) ? m_assetsPackManifest[ASSETS_KEY] : Variant();
        return value.get_type() == Variant::Type::DICTIONARY ? Dictionary(value) : Dictionary();
    }

    const Dictionary Importer::manifestBinDict() {
        const Variant value = m_assetsPackManifest.has(BIN_DATA_KEY) ? m_assetsPackManifest[BIN_DATA_KEY] : Variant();
        return value.get_type() == Variant::Type::DICTIONARY ? Dictionary(value) : Dictionary();
    }

    const Dictionary Importer::manifestTexDict() {
        const Variant value = m_assetsPackManifest.has(TEX_DATA_KEY) ? m_assetsPackManifest[TEX_DATA_KEY] : Variant();
        return value.get_type() == Variant::Type::DICTIONARY ? Dictionary(value) : Dictionary();
    }

    const Array Importer::manifestGroupsArray() {
        const Variant value = m_assetsPackManifest.has(GROUPS_KEY) ? m_assetsPackManifest[GROUPS_KEY] : Variant();
        return value.get_type() == Variant::Type::ARRAY ? Array(value) : Array();
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
        const Ref<FileAccess> jsonFile = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
        if(jsonFile.is_null()) return {};
        Ref<JSON> jsonParser;
        jsonParser.instantiate();
        if(jsonParser->parse(jsonFile->get_as_text()) != OK) return {};
        const Variant parsedData = jsonParser->get_data();
        return parsedData.get_type() == Variant::Type::DICTIONARY ? Dictionary(parsedData) : Dictionary();
    }

    void Importer::ensureManifestSidecarRow(const String& p_table, const String& p_key, const String& p_true_path, int64_t p_weight) {
        ensureManifestDefaultTables();
        Dictionary sidecarTable = Dictionary(m_assetsPackManifest[p_table]);
        if(sidecarTable.has(Variant(p_key))) return;
        sidecarTable[Variant(p_key)] = makeSidecarRow(p_true_path, p_weight);
        m_assetsPackManifest[p_table] = sidecarTable;
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
        const String manifestAbsPath = m_assetPackPath.path_join(String(M_MANIFEST_JSON_FILENAME.c_str()));
        const Ref<FileAccess> writeFile = FileAccess::open(manifestAbsPath, FileAccess::ModeFlags::WRITE);
        if(writeFile.is_null()) {
            UtilityFunctions::push_error(String("Importer: cannot open ")
                + String(M_MANIFEST_JSON_FILENAME.c_str()) + String(" for write: ") + manifestAbsPath);
            return false;
        }
        writeFile->store_string(JSON::stringify(m_assetsPackManifest, "\t"));
        return true;
    }

    void Importer::recomputeGlobalSizesAndCount() {
        const uint64_t totalSizeBytes = computeTotalWeightBytes();
        m_assetsPackManifest["poid_bytes"] = int64_t(totalSizeBytes);
        m_assetsPackManifest["poid"]       = String::num(double(totalSizeBytes) / (1024.0 * 1024.0), 3) + " Mo";
        m_assetsPackManifest["asset"]      = int64_t(manifestAssetsDict().size());
    }

    const uint64_t Importer::computeTotalWeightBytes() {
        uint64_t totalSizeBytes = 0;
        const auto sumWeightsInTable = [&](const Dictionary& sidecarTable) {
            const Array sidecarKeys = sidecarTable.keys();
            for(int i = 0; i < sidecarKeys.size(); ++i) {
                const Dictionary sidecarRow = sidecarTable[sidecarKeys[i]];
                if(sidecarRow.has(WEIGHT_KEY)) totalSizeBytes += uint64_t(int64_t(sidecarRow[WEIGHT_KEY]));
            }
        };
        sumWeightsInTable(manifestAssetsDict());
        sumWeightsInTable(manifestBinDict());
        sumWeightsInTable(manifestTexDict());
        return totalSizeBytes;
    }

    void Importer::pruneEmptyGroupsInManifest() {
        if(!m_assetsPackManifest.has(GROUPS_KEY)) return;
        const Dictionary assetsDict = manifestAssetsDict();
        const Array assetKeys = assetsDict.keys();
        Dictionary usedGroups;
        for(int i = 0; i < assetKeys.size(); ++i) {
            if(!assetsDict.has(assetKeys[i])) continue;
            const Dictionary assetRow = Dictionary(assetsDict[assetKeys[i]]);
            if(!assetRow.has(GROUP_KEY)) continue;
            const String groupName = String(assetRow[GROUP_KEY]).strip_edges();
            if(!groupName.is_empty()) usedGroups[groupName] = true;
        }
        const Array allGroups = manifestGroupsArray();
        Array keptGroups;
        for(int i = 0; i < allGroups.size(); ++i) {
            const String currentGroupName = String(allGroups[i]).strip_edges();
            if(!currentGroupName.is_empty() && usedGroups.has(currentGroupName)) keptGroups.append(currentGroupName);
        }
        m_assetsPackManifest[GROUPS_KEY] = keptGroups;
    }

    bool Importer::recomputeAndWriteManifest(const String& p_pack_root) {
        if(!loadPackManifestFromDiskOrDestruct(p_pack_root, "recomputeAndWriteManifest")) return false;
        return commitPackManifestToDisk(true);
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
        Array sortedKeys = p_dict.keys();
        sortedKeys.sort();
        String fingerprint;
        for(int i = 0; i < sortedKeys.size(); ++i)
            fingerprint += String(sortedKeys[i]) + String("=") + String(p_dict[sortedKeys[i]]) + String(";");
        return fingerprint;
    }

    void Importer::buildAllowedReferencedPackFilenames(Dictionary& p_out_allow) {
        p_out_allow.clear();
        const auto addTableKeysToAllowed = [&](const Dictionary& manifestTable) {
            const Array tableKeys = manifestTable.keys();
            for(int i = 0; i < tableKeys.size(); ++i) p_out_allow[Variant(String(tableKeys[i]))] = true;
        };
        addTableKeysToAllowed(manifestAssetsDict());
        addTableKeysToAllowed(manifestBinDict());
        addTableKeysToAllowed(manifestTexDict());
        p_out_allow[Variant(String(M_MANIFEST_JSON_FILENAME.c_str()))] = true;
    }

    void Importer::recordGltfRowInManifest(const Dictionary& p_item) {
        const String assetPackFilename = p_item["pack_name"];
        Dictionary assetsDict = manifestAssetsDict();
        Dictionary assetRow;
        assetRow[GROUP_KEY]     = p_item["group"];
        assetRow[TRUE_PATH_KEY] = p_item["true_path"];
        assetRow[BIN_ROW_KEY]   = p_item.has(BIN_ROW_KEY)  ? p_item[BIN_ROW_KEY]  : Variant(Dictionary());
        assetRow[TEX_ROW_KEY]   = p_item.has(TEX_ROW_KEY)  ? p_item[TEX_ROW_KEY]  : Variant(Dictionary());
        assetRow[WEIGHT_KEY]    = p_item["gltf_size"];
        assetsDict[Variant(assetPackFilename)] = assetRow;
        m_assetsPackManifest[ASSETS_KEY] = assetsDict;
        const String groupName = p_item["group"].operator String();
        if(!groupName.is_empty()) {
            Array allGroups = manifestGroupsArray();
            bool alreadyListed = false;
            for(int i = 0; i < allGroups.size(); ++i) {
                if(String(allGroups[i]) == groupName) { alreadyListed = true; break; }
            }
            if(!alreadyListed) allGroups.append(groupName);
            m_assetsPackManifest[GROUPS_KEY] = allGroups;
        }
    }

    void Importer::deduplicateOneSidecarTable(const String& p_table_name) {
        if(!m_assetsPackManifest.has(p_table_name)) return;
        Dictionary sidecarTable = m_assetsPackManifest[p_table_name];
        const Array sidecarFilenames = sidecarTable.keys();
        Dictionary deduplicationGroups;
        for(int i = 0; i < sidecarFilenames.size(); ++i) {
            const String sidecarFilename = String(sidecarFilenames[i]);
            if(!sidecarTable.has(Variant(sidecarFilename))) continue;
            const Dictionary sidecarRow = sidecarTable[sidecarFilename];
            if(!sidecarRow.has(TRUE_PATH_KEY) || !sidecarRow.has(WEIGHT_KEY)) continue;
            const String deduplicationKey = String(sidecarRow[TRUE_PATH_KEY]) + String("||") + String::num_int64(int64_t(sidecarRow[WEIGHT_KEY]));
            Array duplicateNames;
            if(deduplicationGroups.has(Variant(deduplicationKey))) duplicateNames = deduplicationGroups[Variant(deduplicationKey)];
            bool alreadyAdded = false;
            for(int vi = 0; vi < duplicateNames.size(); ++vi) {
                if(duplicateNames[vi] == Variant(sidecarFilename)) { alreadyAdded = true; break; }
            }
            if(!alreadyAdded) duplicateNames.append(sidecarFilename);
            deduplicationGroups[Variant(deduplicationKey)] = duplicateNames;
        }
        const Array deduplicationKeys = deduplicationGroups.keys();
        for(int gi = 0; gi < deduplicationKeys.size(); ++gi) {
            Array duplicateNames = deduplicationGroups[deduplicationKeys[gi]];
            if(duplicateNames.size() < 2) continue;
            const String canonicalName = String(duplicateNames[0]);
            int compareIndex = 1;
            while(compareIndex < duplicateNames.size()) {
                const String duplicateName = String(duplicateNames[compareIndex]);
                Dictionary currentTable = Dictionary(m_assetsPackManifest[p_table_name]);
                if(!currentTable.has(Variant(canonicalName)) || !currentTable.has(Variant(duplicateName))) { duplicateNames.remove_at(compareIndex); continue; }
                if(!fileBinaryEqual(m_assetPackPath.path_join(canonicalName), m_assetPackPath.path_join(duplicateName))) { ++compareIndex; continue; }
                mergeRepairPackSidecarsInTable(p_table_name, canonicalName, duplicateName, false);
                duplicateNames.remove_at(compareIndex);
            }
        }
    }

    void Importer::deduplicateGltfAssetsInManifest() {
        if(!m_assetsPackManifest.has(ASSETS_KEY)) return;
        bool progressMade = true;
        while(progressMade) {
            progressMade = false;
            Dictionary assetsDict   = Dictionary(m_assetsPackManifest[ASSETS_KEY]);
            const Array assetKeys   = assetsDict.keys();
            Dictionary assetFingerprints;
            for(int i = 0; i < assetKeys.size(); ++i) {
                const String assetFilename = String(assetKeys[i]);
                if(!assetsDict.has(Variant(assetFilename))) continue;
                const Dictionary assetRow = Dictionary(assetsDict[assetFilename]);
                Dictionary binSidecars, texSidecars;
                extractBinTexMapsFromAssetRow(assetRow, binSidecars, texSidecars);
                const String truePath = assetRow.has(TRUE_PATH_KEY) ? String(assetRow[TRUE_PATH_KEY]) : String();
                assetFingerprints[Variant(assetFilename)] = truePath + String(";") + fingerprintManifestSidecarDict(binSidecars) + String(";")
                    + fingerprintManifestSidecarDict(texSidecars);
            }
            bool merged = false;
            for(int i = 0; i < assetKeys.size() - 1 && !merged; ++i) {
                const String assetFilenameA = String(assetKeys[i]);
                if(!assetsDict.has(Variant(assetFilenameA))) continue;
                const String fingerprintA = String(assetFingerprints[assetFilenameA]);
                for(int secondIndex = i + 1; secondIndex < assetKeys.size() && !merged; ++secondIndex) {
                    const String assetFilenameB = String(assetKeys[secondIndex]);
                    if(!assetsDict.has(Variant(assetFilenameB))) continue;
                    if(String(assetFingerprints[assetFilenameB]) != fingerprintA) continue;
                    if(mergeRepairDuplicateGltfPackFiles(assetFilenameA, assetFilenameB, false, false)) {
                        merged = true; progressMade = true;
                    }
                }
            }
        }
    }

}
