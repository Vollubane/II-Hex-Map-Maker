## Utility autoload for JSON / manifest file operations shared across all UI scripts.
## Centralises read, write, counter-recomputation, path helpers and directory utilities
## so that no individual UI script duplicates this logic.
extends Node

const MANIFEST_FILE       := "manifeste.json"
const ASSET_PACKS_DIR     := "user://Asset Packs"

const _NATIVE_IMPORTER_CLASS  := "Importer"
const _KEY_ASSETS_DATA        := "assets_data"
const _KEY_ASSET              := "asset"
const _KEY_POID               := "poid"
const _KEY_DATE               := "date"
const _SIDECAR_TABLE_KEYS     := ["bin_data", "texture_data"]


## Reads a JSON file and returns its root Dictionary, or {} on any error.
func read_dict(p_path: String) -> Dictionary:
	if not FileAccess.file_exists(p_path):
		return {}
	var json_file := FileAccess.open(p_path, FileAccess.READ)
	if json_file == null:
		return {}
	var parsed: Variant = JSON.parse_string(json_file.get_as_text())
	return parsed if parsed is Dictionary else {}


## Writes p_data as indented JSON to p_path.
func write_dict(p_path: String, p_data: Dictionary) -> Error:
	var write_file := FileAccess.open(p_path, FileAccess.WRITE)
	if write_file == null:
		return FAILED
	write_file.store_string(JSON.stringify(p_data, "\t"))
	return OK


## Recomputes in-place the asset count and total weight fields of an asset pack manifest Dictionary.
func recompute_pack_counters(p_data: Dictionary) -> void:
	var asset_count  := 0
	var total_bytes  := 0
	var assets_table: Variant = p_data.get(_KEY_ASSETS_DATA, {})
	if assets_table is Dictionary:
		for asset_key in (assets_table as Dictionary).keys():
			if str(asset_key).to_lower().ends_with(".gltf"):
				asset_count += 1
			var asset_row: Variant = (assets_table as Dictionary)[asset_key]
			if asset_row is Dictionary:
				total_bytes += int((asset_row as Dictionary).get("weight", 0))
	for sidecar_table_key: String in _SIDECAR_TABLE_KEYS:
		var sidecar_table: Variant = p_data.get(sidecar_table_key, {})
		if sidecar_table is Dictionary:
			for sidecar_key in (sidecar_table as Dictionary).keys():
				var sidecar_row: Variant = (sidecar_table as Dictionary)[sidecar_key]
				if sidecar_row is Dictionary:
					total_bytes += int((sidecar_row as Dictionary).get("weight", 0))
	p_data[_KEY_ASSET]  = asset_count
	p_data["poid_bytes"] = total_bytes
	p_data[_KEY_POID]   = "%.3f Mo" % (float(total_bytes) / (1024.0 * 1024.0))
	p_data[_KEY_DATE]   = Time.get_datetime_dict_from_system()


## Writes an asset pack manifest: uses the C++ Importer to recompute counters when available,
## otherwise falls back to the GDScript recomputation.
## p_pack_dir must be the root folder of the pack (the folder containing manifeste.json).
func write_manifest(p_pack_dir: String, p_data: Dictionary) -> Error:
	var manifest_path := p_pack_dir.path_join(MANIFEST_FILE)
	var initial_write := write_dict(manifest_path, p_data)
	if initial_write != OK:
		return initial_write
	if ClassDB.class_exists(_NATIVE_IMPORTER_CLASS):
		var helper: Object = ClassDB.instantiate(_NATIVE_IMPORTER_CLASS)
		if helper != null:
			var recomputed: bool = helper.call("recomputeAndWriteManifest", p_pack_dir)
			helper.free()
			return OK if recomputed else FAILED
	recompute_pack_counters(p_data)
	return write_dict(manifest_path, p_data)


## Strips surrounding whitespace and removes a trailing .json extension if present.
func sanitize_stem(p_stem: String) -> String:
	var trimmed := p_stem.strip_edges()
	if trimmed.to_lower().ends_with(".json"):
		trimmed = trimmed.substr(0, trimmed.length() - 5)
	return trimmed.strip_edges()


## Normalises a directory path (strips trailing slashes, converts backslashes).
func normalize_dir(p_path: String) -> String:
	return p_path.rstrip("/").replace("\\", "/")


## Returns a sorted list of all asset pack directories (subdirs of ASSET_PACKS_DIR that contain manifeste.json).
func pack_dirs_sorted() -> Array[String]:
	var dir_access := DirAccess.open(ASSET_PACKS_DIR)
	if dir_access == null:
		return []
	var result: Array[String] = []
	dir_access.list_dir_begin()
	var entry := dir_access.get_next()
	while entry != "":
		if dir_access.current_is_dir() and not entry.begins_with("."):
			var sub_dir := ASSET_PACKS_DIR.path_join(entry)
			if FileAccess.file_exists(sub_dir.path_join(MANIFEST_FILE)):
				result.append(sub_dir)
		entry = dir_access.get_next()
	dir_access.list_dir_end()
	result.sort()
	return result


## Returns true if p_nom is already used as the display name ("nom") of any pack
## other than the one located at p_exclude_pack_dir.
func is_pack_nom_taken(p_exclude_pack_dir: String, p_nom: String) -> bool:
	var wanted_nom := p_nom.strip_edges()
	if wanted_nom.is_empty():
		return true
	var excluded_dir := normalize_dir(p_exclude_pack_dir)
	for pack_dir in pack_dirs_sorted():
		if normalize_dir(pack_dir) == excluded_dir:
			continue
		var manifest := read_dict(pack_dir.path_join(MANIFEST_FILE))
		if str(manifest.get("nom", "")).strip_edges() == wanted_nom:
			return true
	return false


## Recursively deletes p_path and all its contents.
func delete_dir_recursive(p_path: String) -> Error:
	var dir_access := DirAccess.open(p_path)
	if dir_access == null:
		return FAILED
	dir_access.list_dir_begin()
	var entry := dir_access.get_next()
	while entry != "":
		if entry != "." and entry != "..":
			var full_path := p_path.path_join(entry)
			if dir_access.current_is_dir():
				var err := delete_dir_recursive(full_path)
				if err != OK:
					dir_access.list_dir_end()
					return err
			else:
				var err := DirAccess.remove_absolute(ProjectSettings.globalize_path(full_path))
				if err != OK:
					dir_access.list_dir_end()
					return err
		entry = dir_access.get_next()
	dir_access.list_dir_end()
	return DirAccess.remove_absolute(ProjectSettings.globalize_path(p_path))
