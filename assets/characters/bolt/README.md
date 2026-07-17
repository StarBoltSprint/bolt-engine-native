# StarBoltSprint — Pure White German Shepherd

Imagine character pack for native Bolt Engine.

## Files

| File | Role |
|------|------|
| `bolt_base.jpg` | Hero ¾ identity lock |
| `bolt_side.jpg` | Side profile turnaround |
| `bolt_front.jpg` | Front turnaround |
| `bolt_back.jpg` | Back turnaround |
| `bolt_sprint.jpg` | Sprint pose |
| `bolt_billboard_src.jpg` | In-game billboard source (chroma magenta) |
| `bolt_fur_src.jpg` | Seamless fur tile source |

Fur PBR (imported): `assets/materials/bolt/bolt_fur_*`

## Runtime

Engine loads `bolt_billboard_src` with **magenta chroma key** → transparent sprite card  
plus `bolt_fur` albedo for micro-detail on the card.

Magenta background must stay pure for clean keys (`#C2185B` family).
