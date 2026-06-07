extends Control

## Emitted when the ListOfAssetPackPanel close (×) button is pressed.
signal close_requested


func _ready() -> void:
	var btn := $ListOfAssetPackPanel.get_node_or_null("Button") as Button
	if btn:
		btn.pressed.connect(func(): close_requested.emit())
