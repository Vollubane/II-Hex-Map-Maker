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
        String n = String(p_rel).replace("\\", "/").trim_prefix("/");
        if(n.is_empty() || p_dq.seen.has(n)) return;
        p_dq.seen[n] = true;
        p_dq.queue.append(n);
    }

    void Importer::drainPackRelativeDeletionQueueSlice(S_DeletionQueue& p_dq) {
        while(0.0 < m_timeBudget && !p_dq.queue.is_empty()) {
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            const String head = String(p_dq.queue[0]);
            p_dq.queue.remove_at(0);
            deletePackRelativeFile(head);
            debitTimeBudgetFromTicks(t0);
        }
    }

    void Importer::deletePackRelativeFile(const String& p_rel_pack_path) {
        const String trimmed = String(p_rel_pack_path).trim_prefix("/");
        const String fp      = m_assetPackPath.path_join(trimmed.replace("\\", "/"));
        const Ref<DirAccess> d = DirAccess::open(fp.get_base_dir());
        if(d.is_valid() && FileAccess::file_exists(fp)) d->remove(fp.get_file());
    }

    void Importer::enumerateFilesInDirNonRecursive(const String& p_abs_path, Array& p_out_filenames) {
        p_out_filenames.clear();
        const Ref<DirAccess> dir = DirAccess::open(p_abs_path);
        if(dir.is_null()) return;
        dir->list_dir_begin();
        String cur = dir->get_next();
        while(cur != String()) {
            if(cur != "." && cur != ".." && !dir->current_is_dir()) p_out_filenames.append(cur);
            cur = dir->get_next();
        }
        dir->list_dir_end();
    }

    void Importer::debitTimeBudgetFromTicks(uint64_t p_slice_start_usec) {
        m_timeBudget -= double(Time::get_singleton()->get_ticks_usec() - p_slice_start_usec);
    }

    const bool Importer::fileBinaryEqual(const String& p_path_a, const String& p_path_b) {
        if(!FileAccess::file_exists(p_path_a) || !FileAccess::file_exists(p_path_b)) return false;
        const Ref<FileAccess> fa = FileAccess::open(p_path_a, FileAccess::ModeFlags::READ);
        const Ref<FileAccess> fb = FileAccess::open(p_path_b, FileAccess::ModeFlags::READ);
        if(fa.is_null() || fb.is_null()) return false;
        const int64_t n = int64_t(fa->get_length());
        if(n != int64_t(fb->get_length())) return false;
        constexpr int64_t k_chunk = 1 << 20;
        int64_t remain = n;
        while(0 < remain) {
            const int64_t take = remain < k_chunk ? remain : k_chunk;
            const PackedByteArray ca = fa->get_buffer(take);
            const PackedByteArray cb = fb->get_buffer(take);
            if(ca.size() != take || cb.size() != take || ca != cb) return false;
            remain -= take;
        }
        return true;
    }

    const bool Importer::dictEqualShallow(const Dictionary& p_a, const Dictionary& p_b) {
        if(p_a.size() != p_b.size()) return false;
        const Array ka = p_a.keys();
        for(int i = 0; i < ka.size(); ++i) {
            const String k = String(ka[i]);
            if(!p_b.has(Variant(k)) || p_a[Variant(k)] != p_b[Variant(k)]) return false;
        }
        return true;
    }

    const bool Importer::tryReplaceJsonQuotedStringInFile(const String& p_path, const String& p_old, const String& p_new) {
        if(p_old == p_new) return true;
        const Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::ModeFlags::READ);
        if(f.is_null()) return false;
        String t = f->get_as_text();
        const String oq = JSON::stringify(Variant(p_old), String(), false, true);
        const String nq = JSON::stringify(Variant(p_new), String(), false, true);
        if(t.find(oq) < 0) return true;
        t = t.replace(oq, nq);
        const Ref<FileAccess> w = FileAccess::open(p_path, FileAccess::ModeFlags::WRITE);
        if(w.is_null()) return false;
        w->store_string(t);
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
