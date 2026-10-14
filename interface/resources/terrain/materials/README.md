# Default terrain materials

The eight layers every terrain starts with (terrain::LayerStack::defaults, libraries/terrain/src/terrain/Layers.h),
reached as `qrc:///terrain/materials/<name>.jpg`. All are the 1K colour maps of CC0 materials from ambientCG
(https://ambientcg.com, Creative Commons CC0 1.0 Universal, no attribution required):

| layer  | ambientCG asset |
|--------|-----------------|
| grass  | Grass004        |
| dirt   | Ground037       |
| rock   | Rock030         |
| sand   | Ground054       |
| gravel | Gravel022       |
| mud    | Ground022       |
| moss   | Moss001         |
| snow   | Snow004         |

`library.json` lists them for the editor's material chooser: name, the colour texture's URL, the tiling in metres
and the plain colour drawn until the texture is in. The same assets' normal, roughness, occlusion and displacement
maps are to come with the texture arrays (TERRAIN.md section 5).
