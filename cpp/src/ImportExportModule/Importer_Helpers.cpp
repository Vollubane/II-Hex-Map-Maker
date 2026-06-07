#include "Importer.h"
#include "Importer_Constants.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace ImportExportModule {

    void Importer::enqueuePackRelativeDeletionRel(S_DeletionQueue& p_dq, const String& p_rel) {
        String normalizedPath = String(p_rel).replace("\\", "/").trim_prefix("/");
        if(normalizedPath.is_empty() || p_dq.seen.has(normalizedPath)) return;
        p_dq.seen[normalizedPath] = true;
        p_dq.queue.append(normalizedPath);
    }

    void Importer::drainPackRelativeDeletionQueueSlice(S_DeletionQueue& p_dq) {
        while(0.0 < m_timeBudget && !p_dq.queue.is_empty()) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const String headEntry = String(p_dq.queue[0]);
            p_dq.queue.remove_at(0);
            deletePackRelativeFile(headEntry);
            debitTimeBudgetFromTicks(t0);
        }
    }

    void Importer::deletePackRelativeFile(const String& p_rel_pack_path) {
        const String trimmedPath = String(p_rel_pack_path).trim_prefix("/");
        const String absolutePath = m_assetPackPath.path_join(trimmedPath.replace("\\", "/"));
        const Ref<DirAccess> parentDir = DirAccess::open(absolutePath.get_base_dir());
        if(parentDir.is_valid() && FileAccess::file_exists(absolutePath)) parentDir->remove(absolutePath.get_file());
    }

    void Importer::enumerateFilesInDirNonRecursive(const String& p_abs_path, Array& p_out_filenames) {
        p_out_filenames.clear();
        const Ref<DirAccess> dir = DirAccess::open(p_abs_path);
        if(dir.is_null()) return;
        dir->list_dir_begin();
        String currentEntry = dir->get_next();
        while(currentEntry != String()) {
            if(currentEntry != "." && currentEntry != ".." && !dir->current_is_dir()) p_out_filenames.append(currentEntry);
            currentEntry = dir->get_next();
        }
        dir->list_dir_end();
    }

    void Importer::debitTimeBudgetFromTicks(uint64_t p_slice_start_usec) {
        m_timeBudget -= double(Time::get_singleton()->get_ticks_usec() - p_slice_start_usec);
    }

    const bool Importer::fileBinaryEqual(const String& p_path_a, const String& p_path_b) {
        if(!FileAccess::file_exists(p_path_a) || !FileAccess::file_exists(p_path_b)) return false;
        const Ref<FileAccess> fileA = FileAccess::open(p_path_a, FileAccess::ModeFlags::READ);
        const Ref<FileAccess> fileB = FileAccess::open(p_path_b, FileAccess::ModeFlags::READ);
        if(fileA.is_null() || fileB.is_null()) return false;
        const int64_t fileSizeBytes = int64_t(fileA->get_length());
        if(fileSizeBytes != int64_t(fileB->get_length())) return false;
        constexpr int64_t k_chunk = 1 << 20;
        int64_t remainingBytes = fileSizeBytes;
        while(0 < remainingBytes) {
            const int64_t chunkSize = remainingBytes < k_chunk ? remainingBytes : k_chunk;
            const PackedByteArray bufferA = fileA->get_buffer(chunkSize);
            const PackedByteArray bufferB = fileB->get_buffer(chunkSize);
            if(bufferA.size() != chunkSize || bufferB.size() != chunkSize || bufferA != bufferB) return false;
            remainingBytes -= chunkSize;
        }
        return true;
    }

    const bool Importer::dictEqualShallow(const Dictionary& p_a, const Dictionary& p_b) {
        if(p_a.size() != p_b.size()) return false;
        const Array keysA = p_a.keys();
        for(int i = 0; i < keysA.size(); ++i) {
            const String key = String(keysA[i]);
            if(!p_b.has(Variant(key)) || p_a[Variant(key)] != p_b[Variant(key)]) return false;
        }
        return true;
    }

    const bool Importer::tryReplaceJsonQuotedStringInFile(const String& p_path, const String& p_old, const String& p_new) {
        if(p_old == p_new) return true;
        const Ref<FileAccess> readFile = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
        if(readFile.is_null()) return false;
        String fileContent = readFile->get_as_text();
        const String oldValueQuoted = JSON::stringify(Variant(p_old), String(), false, true);
        const String newValueQuoted = JSON::stringify(Variant(p_new), String(), false, true);
        if(fileContent.find(oldValueQuoted) < 0) return true;
        fileContent = fileContent.replace(oldValueQuoted, newValueQuoted);
        const Ref<FileAccess> writeFile = FileAccess::open(p_path, FileAccess::ModeFlags::WRITE);
        if(writeFile.is_null()) return false;
        writeFile->store_string(fileContent);
        return true;
    }

    const String Importer::packRelativeCapturePngForGltfPackName(const String& p_gltf_filename) {
        return String("capture/") + p_gltf_filename.get_basename() + String(".png");
    }

    const int64_t Importer::dictGetInt(const Dictionary& p_dict, const Variant& p_key) {
        return p_dict.has(p_key) ? int64_t(p_dict[p_key]) : 0LL;
    }

    void Importer::dictPushToArray(Dictionary& p_dict, const String& p_key, const Variant& p_val) {
        Array arr = p_dict.has(p_key) && p_dict[p_key].get_type() == Variant::Type::ARRAY
            ? Array(p_dict[p_key]) : Array();
        arr.append(p_val);
        p_dict[p_key] = arr;
    }

}
