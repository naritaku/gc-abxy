# GC ABXY

カプセルトイ「コントローラーボタンコレクション」のゲームキューブ ABXY ボタン用 キーパッド化キット（型番 GC ABXY）の、ファームウェアと取扱説明書です。
ボタンを、ワイヤレスと USB で使える 4 キーのキーパッドに作り替えます。

**任天堂株式会社の公式製品ではありません。**（下の注記を参照）

- 取扱説明書: <https://gc-abxy.pages.dev/>
- 最新のファームウェア（`gc-abxy.uf2`）: <https://gc-abxy.pages.dev/fw>
- 問い合わせ: <https://gc-abxy.pages.dev/support>（このリポジトリの Issues）

書き込み方は、取扱説明書の「[プログラムの書き込み](https://gc-abxy.pages.dev/firmware/#flash)」を見てください。

このリポジトリは、非公開の開発リポジトリから自動で書き出しています。Pull Request は受け付けていません。不具合や質問は Issues へどうぞ。

## 構成

| パス | 中身 |
|---|---|
| `firmware/config/` | ZMK の設定（shield `gc_abxy`、keymap、`.conf`、`west.yml`） |
| `firmware/module/gc_abxy/` | 追加モジュール（PROFILE ボタンの長押し管理、LED、CR2450 の残量、デバイス名、診断機能） |
| `site/` | 取扱説明書（Cloudflare Pages で公開）と `/fw`・`/support` の転送設定 |
| `.github/workflows/firmware.yml` | ファームウェアのビルド。`v*` タグで Release に `gc-abxy.uf2` を添付 |
| `.github/workflows/site.yml` | `site/` を Cloudflare Pages へデプロイ |
| `functions/` | Cloudflare Pages Functions（`Accept: text/markdown` で取扱説明書の Markdown 版を返す） |
| `release-notes/` | Release の本文（版ごとに `vX.Y.Z.md`） |

## ファームウェア

- ZMK **v0.3.0**（Zephyr 3.5）に固定。board `seeeduino_xiao_ble`、shield `gc_abxy`
- ZMK Studio（USB）対応。ロックなしで接続できます
- ワイヤレスの接続先は 3 つ
- ファームウェアのバージョン（公開版はタグ `vX.Y.Z`）と診断機能は、取扱説明書の「[困ったとき](https://gc-abxy.pages.dev/help/)」から読めます

手元でビルドする場合:

```bash
cd firmware && west init -l config && west update && west zephyr-export
west build -s zmk/app -d build -b seeeduino_xiao_ble -S studio-rpc-usb-uart -- \
  -DZMK_CONFIG=$PWD/config -DSHIELD=gc_abxy -DZMK_EXTRA_MODULES=$PWD/module/gc_abxy
```

## ライセンス

- `firmware/` と `.github/`: MIT License（[LICENSE](LICENSE)）
- `site/`（取扱説明書の文章・写真・図）: All rights reserved。無断での転載・再配布はできません（[site/LICENSE](site/LICENSE)）

## 注記

任天堂株式会社の公式製品ではありません。同社および関連会社とは一切関係がなく、許諾・後援・提携も受けていません。「ニンテンドーゲームキューブ」「Nintendo」は任天堂株式会社の商標または登録商標です。「コントローラーボタンコレクション」その他の商品名は、各権利者の商標または登録商標です。ここではこれらを、対応するカプセルトイを特定して説明する目的でのみ使用しています。

ファームウェアに含まれるオープンソースソフトウェアとそのライセンスは [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) を参照してください。
