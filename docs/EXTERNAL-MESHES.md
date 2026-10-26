# External test meshes

Twenty public meshes that cover what the corpus, the procedural sets and the real sculpts lack: a
full human body with fingers and ears, creatures with thin legs, horns and tails, a high-genus prop,
cloth and leaf sheets, multi-part props where hard surface meets organic, and dense scans. They are
the `external` set of `tools/bench/corpus.json`.

```bash
python3 tools/bench/fetch_external.py --manifest tools/bench/external.json --out $AR_WORK/meshes/external
python3 tools/bench/ab.py run master --set external          # 3,000 and 10,000 quads
```

`fetch_external.py` downloads each file from a URL pinned to one commit, checks its SHA-256, and
writes a plain triangle OBJ (positions and faces) plus `check.json` (triangles, islands, closed
islands, boundary and non-manifold edges, genus of each closed island). glTF positions are welded
where they are bit-identical, since glTF splits vertices at UV seams; OBJ vertices are kept as they
are. Only `numpy` is needed.

Every mesh here may be used publicly with the credit below (CC0 needs none, but we give it). Test
meshes under research-only terms (Stanford scans) use the same tool with a manifest kept outside the
repo, as the private sculpts do (`AR_CORPUS`); only their metrics enter the repo.

## Meshes and licenses

License as stated by the source, read on 2026-10-22. Shape facts are from `check.json` after
conversion. CC BY-SA 3.0 (lion statue): renders of it must also be offered under CC BY-SA.

| id | What it tests | Triangles | Islands (closed) | Genus | Author / source | License |
|---|---|---:|---|---|---|---|
| ext_makehuman | full human body, hands with fingers, ears (`body` group only) | 26,756 | 1 (1) | 0 | MakeHuman Community, base mesh, [makehuman](https://github.com/makehumancommunity/makehuman) `makehuman/license.txt` C | CC0 1.0 |
| ext_leeperrysmith | head scan with ears | 17,684 | 1 (1) | 0 | Lee Perry-Smith, Infinite-Realities, via three.js examples | CC BY 3.0 |
| ext_washington | marble bust scan | 150,000 | 2 (2) | 0, 0 | Smithsonian NPG.70.4, via [pyvista/vtk-data](https://github.com/pyvista/vtk-data) | CC0 1.0 |
| ext_lincoln | face scan (life mask) | 150,000 | 1 (1) | 0 | Smithsonian, Mills life mask, via pyvista/vtk-data | CC0 1.0 |
| ext_scorpion | thin legs, claws, segmented tail | 99,990 | 1 (1) | 0 | YahooJAPAN ([thingiverse 182363](https://www.thingiverse.com/thing:182363)), remesh by Oded Stein, [odedstein/meshes](https://github.com/odedstein/meshes) | CC BY 3.0 |
| ext_goathead | curled horns, ears (coarse input) | 5,522 | 1 (1) | 0 | hugoelec ([thingiverse 42256](https://www.thingiverse.com/thing:42256)), remesh by Oded Stein | CC BY 3.0 |
| ext_skull | thin bone bridges | 100,002 | 1 (1) | 2 | YahooJAPAN ([thingiverse 182365](https://www.thingiverse.com/thing:182365)), remesh by Oded Stein | CC BY 3.0 |
| ext_tree | thin branches, 0.46M triangles | 459,968 | 1 (1) | 0 | warmarine759 ([thingiverse 3839198](https://www.thingiverse.com/thing:3839198)), modified by Oded Stein | CC BY 4.0 |
| ext_violin | hard surface with thin scroll, bridge and pegs | 255,952 | 1 (1) | 5 | Virtual Museums of Małopolska (Sketchfab), modified by Oded Stein | CC0 1.0 |
| ext_boot | leather, laces and eyelets: high genus | 39,906 | 1 (1) | 12 | inciprocal ([Sketchfab](https://sketchfab.com/3d-models/harrison-work-boot-d9506d9a318e4c56ac3d8ab273bd2540)), modified by Oded Stein | CC BY 4.0 |
| ext_lionstatue | statue scan, mane, open base | 63,338 | 1 (0) | open | tomburtonwood ([thingiverse 25832](https://www.thingiverse.com/thing:25832)), modified by Oded Stein | CC BY-SA 3.0 |
| ext_mosquito | insect scan with hair-thin legs inside an amber shell | 14,349 | 1,208 (1) | open | Loïc Norgeot and Geoffrey Marchal for [Sketchfab](https://sketchfab.com/3d-models/real-time-refraction-demo-mosquito-in-amber-37233d6ed84844fea1ebe88069ea58d1), via [Khronos glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) | CC BY 4.0 |
| ext_scatteringskull | dense skull scan | 188,871 | 1 (0) | open | Khronos glTF-Sample-Assets (ScatteringSkull) | CC0 1.0 |
| ext_barramundi | fish with thin fins (coarse input) | 3,864 | 3 (3) | 0 | Khronos glTF-Sample-Assets (BarramundiFish) | CC0 1.0 |
| ext_corset | cloth scan with lacing, many open parts | 18,324 | 75 (9) | open | UX3D, via Khronos glTF-Sample-Assets | CC0 1.0 |
| ext_plant | leaf cards: open sheets, many islands | 68,409 | 219 (175) | open | Darmstadt Graphics Group, via Khronos glTF-Sample-Assets | CC BY 4.0 |
| ext_sheencloth | draped cloth sheet | 115,200 | 1 (0) | open | Microsoft, via Khronos glTF-Sample-Assets | CC0 1.0 |
| ext_flighthelmet | leather helmet, goggles, straps, stand | 94,722 | 82 (72) | mixed | Khronos glTF-Sample-Assets (FlightHelmet) | CC0 1.0 |
| ext_scifihelmet | hard-surface armor prop | 23,358 | 16 (3) | open | Khronos glTF-Sample-Assets (SciFiHelmet) | CC0 1.0 |
| ext_watch | watch with strap, many small parts, non-manifold edges | 100,002 | 59 (20) | mixed | graphiccompressor ([Sketchfab](https://skfb.ly/oAsPA)), Darmstadt Graphics Group, via Khronos glTF-Sample-Assets | CC BY 4.0 |

Left out on purpose: odedstein's cow, falcon statue, house, strawberry, tower, well, brucewick and
mushroom (CC BY-NC or CC BY-NC-SA), springer (GPL-2.0), Khronos DamagedHelmet (part CC BY-NC 4.0),
BrainStem (Poser EULA), DragonAttenuation and odedstein's armadillo and bunny (Stanford terms: research
use, no commercial use without permission), and most of alecjacobson/common-3d-test-models (no stated
license). pyvista documents the Nefertiti scan it ships as CC BY-NC-SA 4.0 ("The Other Nefertiti"),
which supports keeping our corpus `nefertiti` out of public material (docs/gallery/README.md).

## Hand-downloaded meshes

Seven more meshes come from sites the cloud cannot reach (Sketchfab needs a login; Blender Studio and
Zenodo are blocked by its network policy), so they are not in `external.json`: anyone can download
them by hand from the links below. Their licenses allow public use with credit. Kajetan downloaded
them on 2026-10-23; the converted OBJs, the downloads, their SHA-256 (`hand-manifest.json`) and an
`AR_CORPUS` file (set `external_hand`) live in the project files, not in the repo.

| id | What it tests | Triangles | Islands (closed) | Source | License |
|---|---|---:|---|---|---|
| ext_knight | real speed sculpt: armor, cloth folds, ~4,800 small closed pieces (rings, rivets) | 330,779 | 4,882 (4,142) | [Old knight speedsculpt](https://sketchfab.com/3d-models/old-knight-speedsculpt-ded9aed552c444ef91ab0a5bed65c463) by mrrobot, Sketchfab | CC BY 4.0 |
| ext_tentacle | creature sculpt with tentacles, one closed piece of genus 1 | 393,624 | 1 (1) | [Tentacle Eye Monster](https://sketchfab.com/3d-models/tentacle-eye-monster-e6279ed4b504415d92c18f6ba4994b53) by Scrubmuffin, Sketchfab | CC BY 4.0 |
| ext_phenebub | creature, open pieces | 191,296 | 7 (0) | [Phenebub](https://sketchfab.com/3d-models/phenebub-free-horror-creature-e18dbcb6300b4421aac2bb51cc060aed) by Jorma Rysky, Sketchfab | CC BY 4.0 |
| ext_cosmicbuddha | scan with relief carving | 150,000 | 1 (1) | [Cosmic Buddha](https://zenodo.org/records/10285204), Smithsonian, via Zenodo | CC0 1.0 |
| ext_rain | full character: body, clothes, hair cards, eyes, teeth (18 objects joined) | 79,594 | 98 (16) | [Rain](https://studio.blender.org/characters/rain/v3/), Blender Studio | CC BY |
| ext_rain_hair | Rain's hair cards and hair band only | 6,817 | 18 (1) | as Rain | CC BY |
| ext_sprite | stylized character (14 objects joined) | 35,366 | 33 (10) | [Sprite](https://studio.blender.org/characters/sprite/v1/), Blender Studio | CC BY |

Conversion: `.blend` and `.fbx` with `tools/bench/blender_to_obj.py` (evaluated meshes, viewport
modifiers, world transform, visible objects joined, as the add-on sees them); the glb with
`fetch_external.py`'s reader, which welds UV seams (Blender's own glTF import left the Buddha in 892
open pieces); Phenebub's Collada file with trimesh, welded the same way (Blender 5 has no Collada import).

Credits: Old knight speedsculpt by mrrobot, Tentacle Eye Monster by Scrubmuffin, Phenebub by Jorma
Rysky, all CC BY 4.0 on Sketchfab; Rain and Sprite by Blender Studio, CC BY; Cosmic Buddha by the
Smithsonian Institution, CC0.

## Credits

Scorpion, skull: YahooJAPAN, CC BY 3.0. Goat head: hugoelec, CC BY 3.0. Tree: warmarine759, CC BY 4.0.
Boot: inciprocal, CC BY 4.0. Lion statue: tomburtonwood, CC BY-SA 3.0. Violin: Virtual Museums of
Małopolska, CC0. All six remeshed or modified by Oded Stein (github.com/odedstein/meshes). Head scan:
Lee Perry-Smith (Infinite-Realities), CC BY 3.0. Mosquito in amber: Loïc Norgeot and Geoffrey Marchal
for Sketchfab, CC BY 4.0. Plant and chronograph watch: Darmstadt Graphics Group GmbH (watch started
from "Chronograph Watch Mudmaster" by graphiccompressor), CC BY 4.0. Corset: UX3D, CC0. Sheen cloth:
Microsoft, CC0. Barramundi fish, flight helmet, sci-fi helmet, scattering skull: Khronos
glTF-Sample-Assets, CC0. George Washington bust and Lincoln life mask: Smithsonian Institution, CC0.
MakeHuman base mesh: MakeHuman Community, CC0. Results of remeshing these meshes are adaptations: give
the credit, link the license, and say they were remeshed.
