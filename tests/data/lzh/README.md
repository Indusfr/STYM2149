# LZH test archives

Copied unchanged from the [pylzh](https://github.com/awaku7/pylzh) repository (© 2026 Hirofumi Ukawa, Apache-2.0 licence, see `third_party/licenses/pylzh-LICENSE.txt`), commit `37727d97747e13a23b05c8fbbd55c5685ee6be3f`, folder `tests/`. They are used by `tests/unit/test_lzh.cpp`.

| File | Header level | Method | Contents |
|---|---|---|---|
| `h0_lh0.lzh` | 0 | -lh0- | `gpl-2.gz`, 6829 bytes, CRC $B6D5 |
| `h0_lh5.lzh` | 0 | -lh5- | `gpl-2` (text of the GPL v2), 18092 bytes, CRC $A33A |
| `h1_lh5.lzh` | 1 | -lh5- | same |
| `h2_lh5.lzh` | 2 | -lh5- | same |
| `h2_lh6.lzh` | 2 | -lh6- | same |
| `h2_lh7.lzh` | 2 | -lh7- | same |
| `h2_subdir.lzh` | 2 | -lhd-, -lh0- | two folders, then `hello.txt` ("hello world", 12 bytes, CRC $9778) |
| `lh1-64k.lzh` | 0 | -lh1- | 65536 bytes; unsupported method (refusal expected) |

The text of the GPL v2 in these archives may be copied as is ("Everyone is permitted to copy and distribute verbatim copies").
