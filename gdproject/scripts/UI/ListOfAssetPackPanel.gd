extends Panel

const ASSET_PACKS_DIR := "user://Asset Packs"
const MANIFEST_FILE := "manifeste.json"

const KEY_NOM := "nom"
const KEY_VERSION := "version"
const KEY_POID := "poid"
const KEY_DATE := "date"
const KEY_ASSET := "asset"

const AssetPackDisplayerScene := preload("res://Scene/UI Node/Elements/AssetPackDisplayer.tscn")

@onready var _grid: GridContainer = $"Panel/ScrollContainer/GridContainer"
@onready var _new_pack_button: Button = $"HexMap Button"
@onready var _open_editor_button: Button = $"Edit Button"

var _asset_editor: Node


func _ready() -> void:
	_asset_editor = get_parent().get_node_or_null("AssetPackEditorPanel")
	_open_editor_button.disabled = true
	_open_editor_button.pressed.connect(_on_open_editor_pressed)
	_ensure_packs_dir()
	_new_pack_button.pressed.connect(_on_new_pack_pressed)
	_refresh_pack_list()
	if _asset_editor:
		if _asset_editor.has_signal("editor_closed"):
			_asset_editor.editor_closed.connect(_on_editor_closed)


func _on_editor_closed() -> void:
	_refresh_pack_list()


func _ensure_packs_dir() -> void:
	var da := DirAccess.open("user://")
	if da == null:
		push_error("ListOfAssetPackPanel: cannot open user://")
		return
	if da.dir_exists("Asset Packs"):
		return
	var err := da.make_dir_recursive("Asset Packs")
	if err != OK:
		push_error("ListOfAssetPackPanel: cannot ensure %s (%s)" % [ASSET_PACKS_DIR, err])


func _app_version() -> String:
	return str(ProjectSettings.get_setting("application/config/version", "0.0.1"))


func _read_pack_dict(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {}
	var txt := f.get_as_text()
	var parsed = JSON.parse_string(txt)
	return parsed if parsed is Dictionary else {}


func _write_json(path: String, data: Dictionary) -> Error:
	var json_text := JSON.stringify(data, "\t")
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return FAILED
	f.store_string(json_text)
	return OK


func _minimal_manifest(display_nom: String) -> Dictionary:
	return {
		KEY_NOM: display_nom,
		KEY_VERSION: _app_version(),
		KEY_POID: "0 Mo",
		KEY_DATE: "—",
		KEY_ASSET: 0,
	}


func _sanitize_filename_stem(s: String) -> String:
	var t := s.strip_edges()
	if t.to_lower().ends_with(".json"):
		t = t.substr(0, t.length() - 5)
	return t.strip_edges()


func _display_title(manifest: Dictionary, folder_basename: String) -> String:
	var n: String = str(manifest.get(KEY_NOM, "")).strip_edges()
	return n if not n.is_empty() else folder_basename


func _format_date_field(v: Variant) -> String:
	if v is Dictionary:
		var dt := v as Dictionary
		return "%02d/%02d/%04d" % [
			int(dt.get("day", 1)),
			int(dt.get("month", 1)),
			int(dt.get("year", 1970)),
		]
	return str(v)


func _format_version_for_info(v: String) -> String:
	var t := v.strip_edges()
	if t.is_empty():
		return "V?"
	if t.begins_with("V"):
		return t
	return "V%s" % t


func _format_info_line(data: Dictionary) -> String:
	var ver_s := _format_version_for_info(str(data.get(KEY_VERSION, "?")))
	var poid_s := str(data.get(KEY_POID, "—"))
	var date_s := _format_date_field(data.get(KEY_DATE, "—"))
	var av: Variant = data.get(KEY_ASSET, 0)
	var assets_i: int = 0
	if typeof(av) == TYPE_INT:
		assets_i = av
	elif typeof(av) == TYPE_FLOAT:
		assets_i = int(av)
	else:
		var s := str(av).strip_edges()
		if s.is_valid_float():
			assets_i = int(float(s))
	return "%s | %s | %s | %d asset(s)" % [ver_s, poid_s, date_s, assets_i]


func _pack_dirs_sorted() -> Array[String]:
	var da := DirAccess.open(ASSET_PACKS_DIR)
	if da == null:
		return []
	var out: Array[String] = []
	da.list_dir_begin()
	var entry := da.get_next()
	while entry != "":
		if da.current_is_dir() and not entry.begins_with("."):
			var sub := ASSET_PACKS_DIR.path_join(entry)
			if FileAccess.file_exists(sub.path_join(MANIFEST_FILE)):
				out.append(sub)
		entry = da.get_next()
	da.list_dir_end()
	out.sort()
	return out


func _is_nom_taken_by_other_pack(exclude_pack_dir: String, nom: String) -> bool:
	var want := nom.strip_edges()
	if want.is_empty():
		return true
	var ex := exclude_pack_dir.rstrip("/").replace("\\", "/")
	for d in _pack_dirs_sorted():
		var p := d.rstrip("/").replace("\\", "/")
		if p == ex:
			continue
		var m := _read_pack_dict(d.path_join(MANIFEST_FILE))
		var other := str(m.get(KEY_NOM, "")).strip_edges()
		if other == want:
			return true
	return false


func _refresh_pack_list() -> void:
	if _grid == null:
		return
	for c in _grid.get_children():
		c.queue_free()
	await get_tree().process_frame
	for pack_dir in _pack_dirs_sorted():
		_add_row(pack_dir)
	_update_open_editor_enabled()


func _update_open_editor_enabled() -> void:
	if _open_editor_button == null:
		return
	_open_editor_button.disabled = _selected_displayer() == null


func _add_row(pack_dir: String) -> void:
	var manifest_path := pack_dir.path_join(MANIFEST_FILE)
	var data := _read_pack_dict(manifest_path)
	var folder_basename := pack_dir.get_file()
	var title := _display_title(data, folder_basename)
	var info := _format_info_line(data)
	var d: AssetPackDisplayer = AssetPackDisplayerScene.instantiate()
	d.connect_delete_pressed = false
	_grid.add_child(d)
	d.set_meta("pack_dir", pack_dir)
	d.set_meta("manifest_path", manifest_path)
	d.set_title_and_info(title, info)
	d.title_submitted.connect(_on_row_title_submitted.bind(d))
	d.toggled.connect(_on_displayer_toggled.bind(d))
	d.double_clicked.connect(_on_displayer_double_clicked.bind(d))


func _selected_displayer() -> AssetPackDisplayer:
	for c in _grid.get_children():
		if c is AssetPackDisplayer:
			var ap: AssetPackDisplayer = c as AssetPackDisplayer
			if ap.is_button_pressed():
				return ap
	return null


func _open_editor_for_displayer(d: AssetPackDisplayer) -> void:
	var pack_dir: String = d.get_meta("pack_dir", "")
	if pack_dir.is_empty():
		return
	if _asset_editor and _asset_editor.has_method("open_for_pack"):
		_asset_editor.open_for_pack(pack_dir)


func _on_open_editor_pressed() -> void:
	var d := _selected_displayer()
	if d == null:
		return
	_open_editor_for_displayer(d)


func _on_displayer_toggled(pressed: bool, displayer: AssetPackDisplayer) -> void:
	if pressed:
		for c in _grid.get_children():
			if c == displayer:
				continue
			if c is AssetPackDisplayer:
				(c as AssetPackDisplayer).unpress_button()
	_update_open_editor_enabled()


func _on_displayer_double_clicked(displayer: AssetPackDisplayer) -> void:
	_open_editor_for_displayer(displayer)


func _on_row_title_submitted(new_title: String, displayer: AssetPackDisplayer) -> void:
	var old_dir: String = displayer.get_meta("pack_dir", "")
	var manifest_path: String = displayer.get_meta("manifest_path", old_dir.path_join(MANIFEST_FILE))
	if old_dir.is_empty():
		return
	var old_data := _read_pack_dict(manifest_path)
	var folder_base := old_dir.get_file()
	var restore_title := _display_title(old_data, folder_base)

	var nom := new_title.strip_edges()
	var stem := _sanitize_filename_stem(new_title)
	if stem.is_empty() or nom.is_empty():
		displayer.set_title(restore_title)
		return

	if _is_nom_taken_by_other_pack(old_dir, nom):
		push_warning("ListOfAssetPackPanel: nom déjà utilisé")
		displayer.set_title(restore_title)
		return

	var da := DirAccess.open(ASSET_PACKS_DIR)
	if da == null:
		displayer.set_title(restore_title)
		return

	if stem != folder_base and da.dir_exists(stem):
		push_warning("ListOfAssetPackPanel: dossier déjà existant")
		displayer.set_title(restore_title)
		return

	if stem != folder_base:
		var err := da.rename(folder_base, stem)
		if err != OK:
			push_error("ListOfAssetPackPanel: rename dossier %s" % err)
			displayer.set_title(restore_title)
			return
		old_dir = ASSET_PACKS_DIR.path_join(stem)
		manifest_path = old_dir.path_join(MANIFEST_FILE)
		displayer.set_meta("pack_dir", old_dir)
		displayer.set_meta("manifest_path", manifest_path)

	var m := _read_pack_dict(manifest_path)
	m[KEY_NOM] = nom
	if _write_json(manifest_path, m) != OK:
		push_error("ListOfAssetPackPanel: écriture manifeste")
		displayer.set_title(restore_title)
		return

	displayer.set_title(nom)
	displayer.set_info(_format_info_line(m))


func _allocate_new_pack_folder_name() -> String:
	var da := DirAccess.open(ASSET_PACKS_DIR)
	if da == null:
		return "newpack_%d" % Time.get_ticks_msec()
	for x in range(1, 100000):
		var folder := "newpack%d" % x
		if not da.dir_exists(folder):
			return folder
	return "newpack_%d" % Time.get_ticks_msec()


func _on_new_pack_pressed() -> void:
	_ensure_packs_dir()
	var folder_name := _allocate_new_pack_folder_name()
	var pack_dir := ASSET_PACKS_DIR.path_join(folder_name)
	var da_new := DirAccess.open(ASSET_PACKS_DIR)
	if da_new == null:
		push_error("ListOfAssetPackPanel: ouverture dossier Asset Packs")
		return
	var err := da_new.make_dir(folder_name)
	if err != OK and err != ERR_ALREADY_EXISTS:
		push_error("ListOfAssetPackPanel: création dossier pack (%s)" % err)
		return
	var manifest_path := pack_dir.path_join(MANIFEST_FILE)
	var data := _minimal_manifest(folder_name)
	if _write_json(manifest_path, data) != OK:
		push_error("ListOfAssetPackPanel: écriture manifeste")
		return
	_refresh_pack_list()
	if _asset_editor and _asset_editor.has_method("open_for_pack"):
		_asset_editor.open_for_pack(pack_dir)
