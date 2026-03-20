# iiHexMapMaker - GDExtension C++

Structure CMake minimaliste :

- `CMakeLists.txt` a la racine
- `cpp/CMakeLists.txt` pour la DLL
- `cmake/GDExtensionBindings.cmake` pour recuperer `godot-cpp`

## Build

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Sous Windows avec Visual Studio, la DLL sera generee dans `build/cpp/Debug/cpp.dll`.

Le fichier `gdproject/extensions/ii_hex_map_maker.gdextension` pointe vers ce binaire.
