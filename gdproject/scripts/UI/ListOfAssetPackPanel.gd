extends Panel

const AssetPackDisplayerScene := preload("res://Scene/UI Node/Elements/AssetPackDisplayer.tscn")
const DeletePanelScene := preload("res://Scene/UI Node/Elements/DeletePanel.tscn")

const KEY_NOM     := "nom"
const KEY_VERSION := "version"
const KEY_POID    := "poid"
const KEY_DATE    := "date"
const KEY_ASSET   := "asset"

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
	var dir_access := DirAccess.open("user://")
	if dir_access == null:
		push_error("ListOfAssetPackPanel: cannot open user://")
		return
	if dir_access.dir_exists("Asset Packs"):
		return
	var err := dir_access.make_dir_recursive("Asset Packs")
	if err != OK:
		push_error("ListOfAssetPackPanel: cannot ensure %s (%s)" % [ManifestUtils.ASSET_PACKS_DIR, err])


func _app_version() -> String:
	return str(ProjectSettings.get_setting("application/config/version", "0.0.1"))


func _minimal_manifest(display_nom: String) -> Dictionary:
	return {
		KEY_NOM:     display_nom,
		KEY_VERSION: _app_version(),
		KEY_POID:    "0 Mo",
		KEY_DATE:    "—",
		KEY_ASSET:   0,
	}


func _display_title(manifest: Dictionary, folder_basename: String) -> String:
	var nom: String = str(manifest.get(KEY_NOM, "")).strip_edges()
	return nom if not nom.is_empty() else folder_basename


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
	var trimmed := v.strip_edges()
	if trimmed.is_empty():
		return "V?"
	return trimmed if trimmed.begins_with("V") else "V%s" % trimmed


func _format_info_line(data: Dictionary) -> String:
	var ver_s    := _format_version_for_info(str(data.get(KEY_VERSION, "?")))
	var poid_s   := str(data.get(KEY_POID, "—"))
	var date_s   := _format_date_field(data.get(KEY_DATE, "—"))
	var asset_v: Variant = data.get(KEY_ASSET, 0)
	var asset_count: int = 0
	if typeof(asset_v) == TYPE_INT:
		asset_count = asset_v
	elif typeof(asset_v) == TYPE_FLOAT:
		asset_count = int(asset_v)
	else:
		var s := str(asset_v).strip_edges()
		if s.is_valid_float():
			asset_count = int(float(s))
	return "%s | %s | %s | %d assets" % [ver_s, poid_s, date_s, asset_count]


func _refresh_pack_list() -> void:
	if _grid == null:
		return
	for c in _grid.get_children():
		c.queue_free()
	await get_tree().process_frame
	for pack_dir in ManifestUtils.pack_dirs_sorted():
		_add_row(pack_dir)
	_update_open_editor_enabled()


func _update_open_editor_enabled() -> void:
	if _open_editor_button == null:
		return
	_open_editor_button.disabled = _selected_displayer() == null


func _add_row(pack_dir: String) -> void:
	var manifest_path := pack_dir.path_join(ManifestUtils.MANIFEST_FILE)
	var data := ManifestUtils.read_dict(manifest_path)
	var folder_basename := pack_dir.get_file()
	var title := _display_title(data, folder_basename)
	var info  := _format_info_line(data)
	var displayer: AssetPackDisplayer = AssetPackDisplayerScene.instantiate()
	_grid.add_child(displayer)
	displayer.set_meta("pack_dir", pack_dir)
	displayer.set_meta("manifest_path", manifest_path)
	displayer.set_title_and_info(title, info)
	displayer.title_submitted.connect(_on_row_title_submitted.bind(displayer))
	displayer.toggled.connect(_on_displayer_toggled.bind(displayer))
	displayer.double_clicked.connect(_on_displayer_double_clicked.bind(displayer))
	displayer.delete_pressed.connect(_on_pack_delete_pressed.bind(displayer))


func _selected_displayer() -> AssetPackDisplayer:
	for c in _grid.get_children():
		if c is AssetPackDisplayer:
			var ap: AssetPackDisplayer = c as AssetPackDisplayer
			if ap.is_button_pressed():
				return ap
	return null


func _open_editor_for_displayer(displayer: AssetPackDisplayer) -> void:
	var pack_dir: String = displayer.get_meta("pack_dir", "")
	if pack_dir.is_empty():
		return
	if _asset_editor and _asset_editor.has_method("open_for_pack"):
		_asset_editor.open_for_pack(pack_dir)


func _on_open_editor_pressed() -> void:
	var displayer := _selected_displayer()
	if displayer == null:
		return
	_open_editor_for_displayer(displayer)


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
	var manifest_path: String = displayer.get_meta("manifest_path", old_dir.path_join(ManifestUtils.MANIFEST_FILE))
	if old_dir.is_empty():
		return
	var old_data := ManifestUtils.read_dict(manifest_path)
	var folder_base := old_dir.get_file()
	var restore_title := _display_title(old_data, folder_base)

	var nom  := new_title.strip_edges()
	var stem := ManifestUtils.sanitize_stem(new_title)
	if stem.is_empty() or nom.is_empty():
		displayer.set_title(restore_title)
		return

	if ManifestUtils.is_pack_nom_taken(old_dir, nom):
		push_warning("ListOfAssetPackPanel: pack display name already in use.")
		SystemEventBus.warning_event.emit("Pack name already in use.", 3.5)
		displayer.set_title(restore_title)
		return

	var dir_access := DirAccess.open(ManifestUtils.ASSET_PACKS_DIR)
	if dir_access == null:
		displayer.set_title(restore_title)
		return

	if stem != folder_base and dir_access.dir_exists(stem):
		push_warning("ListOfAssetPackPanel: folder name already exists.")
		SystemEventBus.warning_event.emit("A folder with this name already exists.", 3.5)
		displayer.set_title(restore_title)
		return

	if stem != folder_base:
		var rename_err := dir_access.rename(folder_base, stem)
		if rename_err != OK:
			push_error("ListOfAssetPackPanel: folder rename failed (%s)." % rename_err)
			SystemEventBus.warning_event.emit("Pack rename failed.", 4.0)
			displayer.set_title(restore_title)
			return
		old_dir = ManifestUtils.ASSET_PACKS_DIR.path_join(stem)
		manifest_path = old_dir.path_join(ManifestUtils.MANIFEST_FILE)
		displayer.set_meta("pack_dir", old_dir)
		displayer.set_meta("manifest_path", manifest_path)

	var updated_data := ManifestUtils.read_dict(manifest_path)
	updated_data[KEY_NOM] = nom
	if ManifestUtils.write_dict(manifest_path, updated_data) != OK:
		push_error("ListOfAssetPackPanel: manifest write failed.")
		displayer.set_title(restore_title)
		return

	displayer.set_title(nom)
	displayer.set_info(_format_info_line(updated_data))


func _allocate_new_pack_folder_name() -> String:
	var dir_access := DirAccess.open(ManifestUtils.ASSET_PACKS_DIR)
	if dir_access == null:
		return "newpack_%d" % Time.get_ticks_msec()
	for x in range(1, 100000):
		var folder_name := "newpack%d" % x
		if not dir_access.dir_exists(folder_name):
			return folder_name
	return "newpack_%d" % Time.get_ticks_msec()


func _on_new_pack_pressed() -> void:
	_ensure_packs_dir()
	var folder_name := _allocate_new_pack_folder_name()
	var pack_dir    := ManifestUtils.ASSET_PACKS_DIR.path_join(folder_name)
	var dir_new     := DirAccess.open(ManifestUtils.ASSET_PACKS_DIR)
	if dir_new == null:
		push_error("ListOfAssetPackPanel: cannot open Asset Packs directory.")
		return
	var mkdir_err := dir_new.make_dir(folder_name)
	if mkdir_err != OK and mkdir_err != ERR_ALREADY_EXISTS:
		push_error("ListOfAssetPackPanel: pack folder creation failed (%s)." % mkdir_err)
		return
	var manifest_data := _minimal_manifest(folder_name)
	if ManifestUtils.write_dict(pack_dir.path_join(ManifestUtils.MANIFEST_FILE), manifest_data) != OK:
		push_error("ListOfAssetPackPanel: manifest write failed.")
		return
	_refresh_pack_list()
	if _asset_editor and _asset_editor.has_method("open_for_pack"):
		_asset_editor.open_for_pack(pack_dir)


func _on_pack_delete_pressed(displayer: AssetPackDisplayer) -> void:
	var pack_dir: String = displayer.get_meta("pack_dir", "")
	if pack_dir.is_empty():
		return
	var delete_panel: DeletePanel = DeletePanelScene.instantiate()
	get_parent().add_child(delete_panel)
	delete_panel.layout_mode = 1
	delete_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	delete_panel.move_to_front()
	delete_panel.setup(DeletePanel.FileKind.AssetPack, pack_dir)
	delete_panel.delete_finished.connect(_on_pack_delete_panel_finished)


func _on_pack_delete_panel_finished(pack_dir: String, was_deleted: bool) -> void:
	if not was_deleted:
		return
	var err := ManifestUtils.delete_dir_recursive(pack_dir)
	if err != OK:
		push_error("ListOfAssetPackPanel: failed to delete pack folder (%s): %s" % [err, pack_dir])
		SystemEventBus.warning_event.emit("Pack deletion failed.", 4.0)
		return
	_refresh_pack_list()
