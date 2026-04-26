extends Node3D
## Scène de test pour l'import C++ (manifeste v2, chemins ajustables dans l'inspecteur).

@export var target_pack_name: String = "newpack1"
## Laisser vide pour utiliser `fallback_source_import_path` (KayKit) si le dossier existe.
@export var source_import_path: String = ""
## Utilisé seulement quand `source_import_path` est vide.
@export var fallback_source_import_path: String = "C:/Users/lolou/Desktop/KayKit_Medieval_Hexagon_Pack_1.0_FREE/Assets/gltf"

## Classe C++ enregistrée par la GDExtension (même identifiant que `GDCLASS(Importer, Node)`).
const NATIVE_IMPORTER_CLASS_NAME := "Importer"

var _target_pack_path: String:
	get:
		return "user://Asset Packs/".path_join(target_pack_name)


var _manifest_path: String:
	get:
		return _target_pack_path.path_join("manifeste.json")


func _ready() -> void:
	_ensure_user_pack_and_disk_manifest()
	var src: String = source_import_path.strip_edges()
	if src.is_empty():
		src = fallback_source_import_path.strip_edges()
	if src.is_empty():
		push_error("[LabImporterTest] Aucun chemin d'import: renseignez `source_import_path` dans l'inspecteur.")
		return
	if not _path_is_reachable_dir_or_file(src):
		push_error("[LabImporterTest] Chemin d'import introuvable: %s" % src)
		return
	if not _tree_contains_gltf(src):
		push_error("[LabImporterTest] Aucun fichier .gltf sous: %s" % src)
		return
	if not ClassDB.class_exists(NATIVE_IMPORTER_CLASS_NAME):
		push_error(
			"[LabImporterTest] Classe « %s » absente. Chargez la GDExtension (cpp.dll) : extensions/ii_hex_map_maker.gdextension" % NATIVE_IMPORTER_CLASS_NAME
		)
		return
	var importer: Node = ClassDB.instantiate(NATIVE_IMPORTER_CLASS_NAME) as Node
	if importer == null:
		push_error("[LabImporterTest] Impossible d'instancier l'importer natif.")
		return
	add_child(importer)
	var ok: bool = (importer as Object).call("setupImportNewAssets", _target_pack_path, src) as bool
	print("[LabImporterTest] setupImportNewAssets => ", ok)


func _ensure_user_pack_and_disk_manifest() -> void:
	var da: DirAccess = DirAccess.open("user://")
	if da == null:
		push_error("[LabImporterTest] impossible d'ouvrir user://")
		return
	if not da.dir_exists("Asset Packs"):
		var e: int = da.make_dir_recursive("Asset Packs")
		if e != OK and e != ERR_ALREADY_EXISTS:
			push_error("[LabImporterTest] impossible de créer user://Asset Packs")
			return
	if not da.dir_exists("Asset Packs/".path_join(target_pack_name)):
		var e2: int = da.make_dir_recursive("Asset Packs/".path_join(target_pack_name))
		if e2 != OK and e2 != ERR_ALREADY_EXISTS:
			push_error("[LabImporterTest] impossible de créer le pack lab")
			return
	if not FileAccess.file_exists(_manifest_path):
		_write_fresh_manifest_file()
		return
	_patch_manifest_for_importer_v2()


func _default_manifest() -> Dictionary:
	return {
		"nom": target_pack_name,
		"version": "0.0.1",
		"poid": "0 Mo",
		"poid_bytes": 0,
		"date": "—",
		"asset": 0,
		"assets_data": {},
		"bin_data": {},
		"texture_data": {},
		"groups": [],
	}


func _write_fresh_manifest_file() -> void:
	var f: FileAccess = FileAccess.open(_manifest_path, FileAccess.WRITE)
	if f == null:
		push_error("[LabImporterTest] impossible d'écrire manifeste.json")
		return
	f.store_string(JSON.stringify(_default_manifest(), "\t"))


func _patch_manifest_for_importer_v2() -> void:
	if not FileAccess.file_exists(_manifest_path):
		_write_fresh_manifest_file()
		return
	var t: String = FileAccess.get_file_as_string(_manifest_path)
	if t.is_empty():
		_write_fresh_manifest_file()
		return
	var json: JSON = JSON.new()
	if json.parse(t) != OK:
		push_warning("[LabImporterTest] manifeste corrompu, recréation du gabarit v2")
		_write_fresh_manifest_file()
		return
	var d: Variant = json.data
	if typeof(d) != TYPE_DICTIONARY:
		_write_fresh_manifest_file()
		return
	var m: Dictionary = d
	var base: Dictionary = _default_manifest()
	for k in base:
		if not m.has(k):
			m[k] = base[k]
	var out: FileAccess = FileAccess.open(_manifest_path, FileAccess.WRITE)
	if out:
		out.store_string(JSON.stringify(m, "\t"))


func _path_is_reachable_dir_or_file(absolute_p: String) -> bool:
	if FileAccess.file_exists(absolute_p):
		return true
	if DirAccess.open(absolute_p) != null:
		return true
	return false


func _tree_contains_gltf(root_path: String) -> bool:
	if FileAccess.file_exists(root_path) and str(root_path).to_lower().ends_with(".gltf"):
		return true
	var dir: DirAccess = DirAccess.open(root_path)
	if dir == null:
		return false
	dir.list_dir_begin()
	var n: String = dir.get_next()
	while n != "":
		if n != "." and n != "..":
			var child: String = root_path.path_join(n)
			if dir.current_is_dir():
				if _tree_contains_gltf(child):
					dir.list_dir_end()
					return true
			elif n.to_lower().ends_with(".gltf"):
				dir.list_dir_end()
				return true
		n = dir.get_next()
	dir.list_dir_end()
	return false
