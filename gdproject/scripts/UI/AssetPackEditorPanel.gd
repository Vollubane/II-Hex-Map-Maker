class_name AssetPackEditorPanel
extends Panel

## Emitted when the editor closes (×); parent lists may refresh.
signal editor_closed

const NATIVE_IMPORTER_CLASS_NAME := "Importer"

const AssetDisplayerScene: PackedScene = preload("res://Scene/UI Node/Elements/AssetDisplayer.tscn")

const ASSET_PACKS_DIR := "user://Asset Packs"
const MANIFEST_FILE := "manifeste.json"

const KEY_NOM := "nom"
const KEY_VERSION := "version"
const KEY_POID := "poid"
const KEY_DATE := "date"
const KEY_ASSET := "asset"
const KEY_ASSETS_DATA := "assets_data"
const KEY_GROUPS := "groups"

const _FORBIDDEN_FILENAME_CHARS: Array[String] = ["/", "\\", "\"", ":", "*", "?", "<", ">", "|"]

const _WINDOWS_RESERVED_NAMES: Array[String] = [
	"CON", "PRN", "AUX", "NUL",
	"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
	"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
]

@onready var _close_button: Button = $Button
@onready var _name_label: Label = $VBoxContainer/DetailPanel/HBoxContainerDetail/NamePanel/HBoxContainer/Name
@onready var _name_row: HBoxContainer = $VBoxContainer/DetailPanel/HBoxContainerDetail/NamePanel/HBoxContainer
@onready var _edit_name_button: Button = $VBoxContainer/DetailPanel/HBoxContainerDetail/NamePanel/HBoxContainer/EditNameButton
@onready var _version_label: Label = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/VersionPanel/VBoxContainer/VersionValuePanel/HBoxContainer/Version
)
@onready var _version_row: HBoxContainer = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/VersionPanel/VBoxContainer/VersionValuePanel/HBoxContainer
)
@onready var _edit_version_button: Button = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/VersionPanel/VBoxContainer/VersionValuePanel/HBoxContainer/EditVersionButton
)
@onready var _group_count_label: Label = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/CountDataPanel/VBoxContainer/PanelGroupDetail/HBoxContainerGroupDetail/GroupNumber
)
@onready var _assets_count_label: Label = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/CountDataPanel/VBoxContainer/PanelAssetsDetail/HBoxContainerAssetsDetail/AssetsNumber
)

@onready var _import_button: Button = $VBoxContainer/AssetUtilitaryButtons/HBoxContainer/ImportButton
@onready var _delete_selected_button: Button = $VBoxContainer/AssetUtilitaryButtons/HBoxContainer/SupAssetsButton
@onready var _repair_pack_button: Button = $VBoxContainer/AssetUtilitaryButtons/HBoxContainer/ManageGroupButton2

@onready var _assets_grid: GridContainer = $VBoxContainer/AssetContainer/ScrollContainer/AssetsGrid

var _pack_dir: String = ""

var _name_line_edit: LineEdit
var _version_line_edit: LineEdit
var _saved_name_for_edit: String = ""
var _saved_version_for_edit: String = ""
var _suppress_name_focus: bool = false
var _suppress_version_focus: bool = false

var _file_dialog: FileDialog
var _importer_busy: bool = false


func _ready() -> void:
	visible = false
	_import_button.tooltip_text = "Choisir un dossier : tous les fichiers .gltf qu’il contient (y compris sous-dossiers) sont ajoutés au pack."
	_close_button.pressed.connect(_on_close_pressed)
	_edit_name_button.pressed.connect(_begin_name_edit)
	_edit_version_button.pressed.connect(_begin_version_edit)
	_import_button.pressed.connect(_on_import_assets_pressed)
	_delete_selected_button.pressed.connect(_on_delete_selected_assets_pressed)
	_repair_pack_button.pressed.connect(_on_repair_pack_pressed)

	_name_line_edit = LineEdit.new()
	_name_line_edit.visible = false
	_name_line_edit.custom_minimum_size = Vector2(280, 40)
	_name_line_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_name_row.add_child(_name_line_edit)
	_name_row.move_child(_name_line_edit, _name_label.get_index() + 1)
	_name_line_edit.text_submitted.connect(_on_name_line_submitted)
	_name_line_edit.focus_exited.connect(_on_name_focus_exited)

	_version_line_edit = LineEdit.new()
	_version_line_edit.visible = false
	_version_line_edit.custom_minimum_size = Vector2(75, 0)
	_version_row.add_child(_version_line_edit)
	_version_row.move_child(_version_line_edit, _version_label.get_index() + 1)
	_version_line_edit.text_submitted.connect(_on_version_line_submitted)
	_version_line_edit.focus_exited.connect(_on_version_focus_exited)


func _ensure_import_file_dialog() -> void:
	if _file_dialog != null:
		return
	_file_dialog = FileDialog.new()
	_file_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_file_dialog.use_native_dialog = true
	## Dossier source : l’importeur enregistre tous les `.gltf` trouvés sous ce chemin (récursif).
	_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	_file_dialog.title = "Choisir le dossier d’assets (fichiers .gltf inclus)"
	add_child(_file_dialog)
	_file_dialog.dir_selected.connect(_on_import_folder_selected)


func _input(event: InputEvent) -> void:
	if _name_line_edit.visible:
		if _click_outside_control(event, _name_line_edit):
			_cancel_name_edit()
			get_viewport().set_input_as_handled()
			return
	if _version_line_edit.visible:
		if _click_outside_control(event, _version_line_edit):
			_cancel_version_edit()
			get_viewport().set_input_as_handled()


func _click_outside_control(event: InputEvent, le: LineEdit) -> bool:
	if not (event is InputEventMouseButton):
		return false
	var mb := event as InputEventMouseButton
	if not mb.pressed or mb.button_index != MOUSE_BUTTON_LEFT:
		return false
	return not le.get_global_rect().has_point(mb.global_position)


func open_for_pack(pack_dir: String) -> void:
	_pack_dir = _norm_dir(pack_dir)
	await _refresh_all_from_manifest()
	var list := get_parent().get_node_or_null("ListOfAssetPackPanel") as Control
	if list:
		list.visible = false
	visible = true


func _manifest_path() -> String:
	return _pack_dir.path_join(MANIFEST_FILE)


func _refresh_pack_header(dict: Dictionary) -> void:
	var folder := _pack_dir.get_file()
	var nom := str(dict.get(KEY_NOM, "")).strip_edges()
	if nom.is_empty():
		nom = folder
	_name_label.text = nom
	_version_label.text = str(dict.get(KEY_VERSION, ""))


func _update_stats(dict: Dictionary) -> void:
	var groups: Variant = dict.get(KEY_GROUPS, [])
	var gcount := 0
	if groups is Array:
		gcount = groups.size()
	_group_count_label.text = str(gcount)

	var assets: Variant = dict.get(KEY_ASSETS_DATA, {})
	var acount := 0
	if assets is Dictionary:
		for kk in assets.keys():
			var ks := str(kk)
			if ks.to_lower().ends_with(".gltf"):
				acount += 1
	_assets_count_label.text = str(acount)


## Libellé UI : clé interne / fichier = `…/foo.gltf`, affichage = `foo` (sans extension).
func _asset_display_title(gltf_manifest_key: String) -> String:
	return gltf_manifest_key.get_file().get_basename()


func _manifest_gltf_keys_sorted(data: Dictionary) -> PackedStringArray:
	var assets: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(assets) != TYPE_DICTIONARY:
		return PackedStringArray()
	var bucket: Array[String] = []
	for kk in assets.keys():
		var ks := str(kk)
		if ks.to_lower().ends_with(".gltf"):
			bucket.append(ks)
	bucket.sort()
	var ps := PackedStringArray()
	for s in bucket:
		ps.append(s)
	return ps


func _refresh_all_from_manifest() -> void:
	if _pack_dir.is_empty():
		return
	var d := _read_pack_dict(_manifest_path())
	_refresh_pack_header(d)
	_update_stats(d)
	await _rebuild_assets_grid(d)


func _clear_assets_grid() -> void:
	for c in _assets_grid.get_children():
		c.queue_free()


func _rebuild_assets_grid(data: Dictionary) -> void:
	_clear_assets_grid()
	await get_tree().process_frame
	for gltf_k in _manifest_gltf_keys_sorted(data):
		var disp: AssetDisplayer = AssetDisplayerScene.instantiate() as AssetDisplayer
		_assets_grid.add_child(disp)
		disp.set_meta("gltf_key", gltf_k)
		disp.set_preview_and_title(_load_capture_thumbnail(gltf_k), _asset_display_title(gltf_k))
		disp.title_submitted.connect(_on_asset_title_submitted.bind(disp))


func _refresh_single_displayer(displayer: AssetDisplayer, gltf_key: String) -> void:
	displayer.set_meta("gltf_key", gltf_key)
	displayer.set_preview_and_title(_load_capture_thumbnail(gltf_key), _asset_display_title(gltf_key))


func _load_capture_thumbnail(gltf_filename: String) -> Texture2D:
	var png_rel := _pack_dir.path_join("capture").path_join(gltf_filename.get_basename() + ".png")
	if not FileAccess.file_exists(png_rel):
		return null
	var abs_path := ProjectSettings.globalize_path(png_rel)
	var img := Image.load_from_file(abs_path)
	if img == null:
		return null
	return ImageTexture.create_from_image(img)


func _set_action_buttons_busy(busy: bool) -> void:
	_import_button.disabled = busy
	_delete_selected_button.disabled = busy
	_repair_pack_button.disabled = busy


func _on_import_assets_pressed() -> void:
	if _importer_busy or _pack_dir.is_empty():
		return
	_ensure_import_file_dialog()
	# Native dialogs ignore centered size hints; embedded popup uses them if use_native_dialog is false.
	_file_dialog.popup()


func _on_import_folder_selected(absolute_folder: String) -> void:
	var p := absolute_folder.strip_edges()
	if p.is_empty():
		return
	_run_importer_call(func(importer: Node) -> void:
		var ok := importer.call("setupImportNewAssets", _pack_dir, p) as bool
		if not ok:
			push_warning("AssetPackEditorPanel: import depuis dossier impossible (voir console).")
	)


func _on_delete_selected_assets_pressed() -> void:
	if _importer_busy or _pack_dir.is_empty():
		return
	var sel := _selected_gltf_keys()
	if sel.is_empty():
		push_warning("AssetPackEditorPanel: no assets selected to remove.")
		return
	var arr: Array = []
	for s in sel:
		arr.append(str(s))
	_run_importer_call(func(importer: Node) -> void:
		importer.call("setupRemoveAssetsFromPack", _pack_dir, arr)
	)


func _on_repair_pack_pressed() -> void:
	if _importer_busy or _pack_dir.is_empty():
		return
	_run_importer_call(func(importer: Node) -> void:
		var ok := importer.call("setupRepareAssetsPack", _pack_dir) as bool
		if not ok:
			push_warning("AssetPackEditorPanel: repair setup failed.")
	)


func _run_importer_call(setup_cb: Callable) -> void:
	if _importer_busy:
		return
	if not ClassDB.class_exists(NATIVE_IMPORTER_CLASS_NAME):
		push_error(
			"AssetPackEditorPanel: native `%s` missing. Load the GDExtension (cpp.dll)." % NATIVE_IMPORTER_CLASS_NAME
		)
		return
	var importer := ClassDB.instantiate(NATIVE_IMPORTER_CLASS_NAME) as Node
	if importer == null:
		push_error("AssetPackEditorPanel: could not instantiate native Importer.")
		return
	_importer_busy = true
	_set_action_buttons_busy(true)
	add_child(importer)
	importer.tree_exited.connect(_on_importer_finished, CONNECT_ONE_SHOT)
	setup_cb.call(importer)


func _on_importer_finished() -> void:
	_importer_busy = false
	_set_action_buttons_busy(false)
	call_deferred("_refresh_all_from_manifest_deferred")


func _refresh_all_from_manifest_deferred() -> void:
	await _refresh_all_from_manifest()


func _selected_gltf_keys() -> Array[String]:
	var out: Array[String] = []
	for c in _assets_grid.get_children():
		if c is AssetDisplayer:
			var d := c as AssetDisplayer
			if d.is_button_pressed():
				var k := str(d.get_meta("gltf_key", "")).strip_edges()
				if not k.is_empty():
					out.append(k)
	return out


func _normalize_gltf_filename(s: String) -> String:
	var t := s.strip_edges().replace("\\", "/")
	var base := t.get_file()
	if base.is_empty():
		return ""
	base = base.get_basename() + ".gltf"
	return base


func _is_windows_plain_filename_ok(name_part: String) -> bool:
	if name_part.strip_edges() != name_part:
		return false
	if name_part.ends_with(" ") or name_part.ends_with("."):
		return false
	for i in range(name_part.length()):
		var code := name_part.unicode_at(i)
		if code <= 31:
			return false
	for ch in _FORBIDDEN_FILENAME_CHARS:
		if name_part.contains(ch):
			return false
	return true


func _is_windows_reserved_stem(gltf_key: String) -> bool:
	var stem := gltf_key.get_basename().strip_edges().to_upper()
	if stem.is_empty():
		return true
	return stem in _WINDOWS_RESERVED_NAMES


func _title_contains_forbidden_chars(s: String) -> bool:
	for ch in _FORBIDDEN_FILENAME_CHARS:
		if s.contains(ch):
			return true
	return false


func _gltf_name_conflicts(candidate: String, old_key_in_manifest: String) -> bool:
	var data := _read_pack_dict(_manifest_path())
	var av: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(av) != TYPE_DICTIONARY:
		return false
	var ad: Dictionary = av
	for k in ad.keys():
		var ks := str(k)
		if not ks.to_lower().ends_with(".gltf"):
			continue
		if ks == old_key_in_manifest:
			continue
		if ks.to_lower() == candidate.to_lower():
			return true
	return false


func _rename_asset_files(old_key: String, new_key: String) -> Error:
	var da := DirAccess.open(_pack_dir)
	if da == null:
		return ERR_CANT_OPEN
	if old_key.to_lower() == new_key.to_lower():
		if not da.file_exists(old_key):
			return OK
		if old_key != new_key and da.file_exists(new_key):
			return ERR_ALREADY_EXISTS
		if old_key != new_key:
			return da.rename(old_key, new_key)
		return OK
	if not da.file_exists(old_key):
		return ERR_DOES_NOT_EXIST
	if da.file_exists(new_key):
		return ERR_ALREADY_EXISTS
	var e := da.rename(old_key, new_key)
	if e != OK:
		return e
	var cdir := DirAccess.open(_pack_dir.path_join("capture"))
	if cdir:
		var op := old_key.get_basename() + ".png"
		var np := new_key.get_basename() + ".png"
		if cdir.file_exists(op) and not cdir.file_exists(np):
			cdir.rename(op, np)
	return OK


func _on_asset_title_submitted(new_text: String, displayer: AssetDisplayer) -> void:
	var old_key := str(displayer.get_meta("gltf_key", "")).strip_edges()
	if old_key.is_empty():
		return
	var new_key := _normalize_gltf_filename(new_text)
	if new_key.is_empty():
		push_warning("AssetPackEditorPanel: asset name cannot be empty.")
		_refresh_single_displayer(displayer, old_key)
		return
	var base_only := new_key.get_basename()
	if not _is_windows_plain_filename_ok(base_only):
		push_warning("AssetPackEditorPanel: illegal characters or trailing separator in asset name.")
		_refresh_single_displayer(displayer, old_key)
		return
	if _title_contains_forbidden_chars(base_only) or _is_windows_reserved_stem(new_key):
		push_warning("AssetPackEditorPanel: illegal file name for Windows.")
		_refresh_single_displayer(displayer, old_key)
		return
	if new_key == old_key:
		return
	if _gltf_name_conflicts(new_key, old_key):
		push_warning("AssetPackEditorPanel: asset name already in use.")
		_refresh_single_displayer(displayer, old_key)
		return

	var data := _read_pack_dict(_manifest_path())
	var assets_variant: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(assets_variant) != TYPE_DICTIONARY:
		push_warning("AssetPackEditorPanel: invalid manifest (assets_data).")
		_refresh_single_displayer(displayer, old_key)
		return
	var assets_dict: Dictionary = assets_variant
	if not assets_dict.has(old_key):
		push_warning("AssetPackEditorPanel: asset key missing from manifest.")
		_refresh_single_displayer(displayer, old_key)
		return

	if old_key.to_lower() != new_key.to_lower():
		var fe := _rename_asset_files(old_key, new_key)
		if fe != OK:
			push_error("AssetPackEditorPanel: failed to rename files on disk (%s)." % fe)
			_refresh_single_displayer(displayer, old_key)
			return

	var row_variant: Variant = assets_dict[old_key]
	assets_dict.erase(old_key)
	assets_dict[new_key] = row_variant
	data[KEY_ASSETS_DATA] = assets_dict
	if _write_json(_manifest_path(), data) != OK:
		push_error("AssetPackEditorPanel: manifest write failed after rename.")
	displayer.set_meta("gltf_key", new_key)
	await _refresh_all_from_manifest()


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


func _sanitize_filename_stem(s: String) -> String:
	var t := s.strip_edges()
	if t.to_lower().ends_with(".json"):
		t = t.substr(0, t.length() - 5)
	return t.strip_edges()


func _norm_dir(p: String) -> String:
	return p.rstrip("/").replace("\\", "/")


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
	var ex := _norm_dir(exclude_pack_dir)
	for d in _pack_dirs_sorted():
		var p := _norm_dir(d)
		if p == ex:
			continue
		var m := _read_pack_dict(d.path_join(MANIFEST_FILE))
		var other := str(m.get(KEY_NOM, "")).strip_edges()
		if other == want:
			return true
	return false


func _begin_name_edit() -> void:
	_saved_name_for_edit = _name_label.text
	_name_line_edit.text = _saved_name_for_edit
	_name_label.visible = false
	_name_line_edit.visible = true
	set_process_input(true)
	_name_line_edit.grab_focus()
	_name_line_edit.call_deferred("select_all")


func _cancel_name_edit() -> void:
	if not _name_line_edit.visible:
		return
	_name_line_edit.text = _saved_name_for_edit
	_name_label.text = _saved_name_for_edit
	_name_line_edit.visible = false
	_name_label.visible = true
	set_process_input(false)


func _on_name_line_submitted(new_text: String) -> void:
	if _title_contains_forbidden_chars(new_text):
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return
	var nom := new_text.strip_edges()
	var stem := _sanitize_filename_stem(new_text)
	var folder_base := _pack_dir.get_file()
	var mp := _manifest_path()
	var data := _read_pack_dict(mp)

	if stem.is_empty() or nom.is_empty():
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if _is_nom_taken_by_other_pack(_pack_dir, nom):
		push_warning("AssetPackEditorPanel: pack display name already in use.")
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	var da := DirAccess.open(ASSET_PACKS_DIR)
	if da == null:
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if stem != folder_base and da.dir_exists(stem):
		push_warning("AssetPackEditorPanel: folder name already exists.")
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if stem != folder_base:
		var err_rn := da.rename(folder_base, stem)
		if err_rn != OK:
			push_error("AssetPackEditorPanel: folder rename failed (%s)." % err_rn)
			_suppress_name_focus = true
			_cancel_name_edit()
			call_deferred("_clear_name_suppress")
			return
		_pack_dir = ASSET_PACKS_DIR.path_join(stem)
		mp = _manifest_path()

	data = _read_pack_dict(mp)
	data[KEY_NOM] = nom
	if _write_json(mp, data) != OK:
		push_error("AssetPackEditorPanel: manifest write failed.")
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	_suppress_name_focus = true
	_name_label.text = nom
	_name_line_edit.text = nom
	_name_line_edit.visible = false
	_name_label.visible = true
	set_process_input(false)
	call_deferred("_clear_name_suppress")


func _clear_name_suppress() -> void:
	_suppress_name_focus = false


func _on_name_focus_exited() -> void:
	if _suppress_name_focus:
		return
	if not _name_line_edit.visible:
		return
	_cancel_name_edit()


func _begin_version_edit() -> void:
	_saved_version_for_edit = _version_label.text
	_version_line_edit.text = _saved_version_for_edit
	_version_label.visible = false
	_version_line_edit.visible = true
	set_process_input(true)
	_version_line_edit.grab_focus()
	_version_line_edit.call_deferred("select_all")


func _cancel_version_edit() -> void:
	if not _version_line_edit.visible:
		return
	_version_line_edit.text = _saved_version_for_edit
	_version_label.text = _saved_version_for_edit
	_version_line_edit.visible = false
	_version_label.visible = true
	if not _name_line_edit.visible:
		set_process_input(false)


func _on_version_line_submitted(new_text: String) -> void:
	var mp := _manifest_path()
	var data := _read_pack_dict(mp)
	data[KEY_VERSION] = new_text.strip_edges()
	if _write_json(mp, data) != OK:
		push_error("AssetPackEditorPanel: manifest write failed (version).")
		_suppress_version_focus = true
		_cancel_version_edit()
		call_deferred("_clear_version_suppress")
		return
	_suppress_version_focus = true
	_version_label.text = data[KEY_VERSION]
	_version_line_edit.visible = false
	_version_label.visible = true
	if not _name_line_edit.visible:
		set_process_input(false)
	call_deferred("_clear_version_suppress")


func _clear_version_suppress() -> void:
	_suppress_version_focus = false


func _on_version_focus_exited() -> void:
	if _suppress_version_focus:
		return
	if not _version_line_edit.visible:
		return
	_cancel_version_edit()


func _on_close_pressed() -> void:
	if _name_line_edit.visible:
		_cancel_name_edit()
	if _version_line_edit.visible:
		_cancel_version_edit()
	visible = false
	var list := get_parent().get_node_or_null("ListOfAssetPackPanel") as Control
	if list:
		list.visible = true
	editor_closed.emit()
