# foot (patched by adachi)

this is a modified build of foot with kitty graphics protocol support (`t=f`, PNG/RGBA, `c=`/`r=` sizing, chunked transfers).

for full documentation, configuration, and the original project, see:
https://codeberg.org/dnkl/foot

## changes in this fork

- kitty graphics protocol support via APC `ESC _ G … ST`
- inline image rendering through the terminal image pipeline
- file-transfer medium (`t=f`, including fastfetch/chafa style workflows)
- `c=` / `r=` sizing support for placements
- updated package build target: `foot-kgp`

## Example

![Kitty graphics example](doc/kitty-example.png)
