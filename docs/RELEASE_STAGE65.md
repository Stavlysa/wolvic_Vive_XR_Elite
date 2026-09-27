> Unofficial community build for VIVE XR Elite, based on Wolvic and HTC Wave 5.6.

## TL;DR

Browse with your real surroundings as the background. Passthrough controls now live in Environment, with additional fixes for browser transparency and immersive rendering.

## 繁體中文 TL;DR

現在可以一邊看見真實環境、一邊瀏覽網頁。透視開關放在 Environment，並修正介面透明度與部分沉浸式渲染處理。

## Full changelog since the previous GitHub release

### Passthrough background and settings

- Added real-world passthrough behind browser windows on VIVE XR Elite.
- **Passthrough background — On/Off:** immediately switch the current background in **Settings > Environment**. The three-dot menu shortcut remains available.
- **Start with Passthrough Mode — On/Off:** independently choose whether the next launch starts in passthrough.
- Selecting a virtual environment turns off passthrough for the current session. Reopening Environment reflects the current setting without changing it.
- Reset Environment Settings now owns the passthrough options; Reset Display Settings no longer changes them.
- Added passthrough restoration across pause/resume and transitions out of opaque immersive VR, with an error message if the runtime rejects the request.

### Browser display and immersive rendering

- Corrected transparency compositing for flat and curved windows, text edges and toolbars while preserving transparent rounded corners.
- WebXR texture copying now uses each eye's supplied texture region instead of assuming fixed stereo halves.
- Added checks for unavailable or invalid Wave texture slots and incomplete framebuffer queues during rendering-mode and size transitions.
- Improved retry handling for failed surface lookups and corrected the surface-handle JNI argument type.
- Tightened frame acquisition, release and acknowledgement handling so completion is published with a fresh head pose.
- Fixed fragment-shader cleanup.

### Known limitations

- Some distant immersive WebXR background geometry may still flicker intermittently.
- Large controller rotations during window dragging may still feel nonlinear.
- Browser passthrough does not add immersive WebXR AR support.

## 繁體中文完整更新日誌

### 透視背景與設定

- VIVE XR Elite 現在可以在瀏覽器視窗後方顯示真實環境。
- **透視背景 — 開／關：** 在 **Settings > Environment** 即時切換目前背景；三點選單的快捷入口也保留。
- **啟動時使用透視 — 開／關：** 獨立控制下次啟動是否使用透視背景。
- 選取虛擬環境時，會關閉目前的透視背景。重新打開 Environment 會顯示目前狀態，不會自行切換背景。
- 重設 Environment 設定會一併重設透視選項；重設 Display 設定不再影響透視選項。
- 加入暫停／恢復及退出不透明沉浸式 VR 後的透視恢復處理；系統拒絕啟用時會顯示錯誤提示。

### 瀏覽器顯示與沉浸式渲染

- 修正平面與曲面網頁視窗、文字邊緣及工具列的透明度合成，保留圓角外側的透明效果。
- WebXR 貼圖複製改為使用每隻眼睛提供的取樣範圍，不再固定假設為左右各半。
- 加入 Wave 貼圖槽位與畫面緩衝佇列檢查，改善渲染模式或尺寸切換時遇到無效資料的處理。
- 改善找不到網頁繪圖表面時的重試處理，並修正表面識別碼的 JNI 參數型別。
- 補強幀取得、釋放與確認流程，讓完成狀態與新的頭部姿態一起送出。
- 修正片段著色器的資源釋放。

### 已知限制

- 部分沉浸式 WebXR 場景的遠處背景仍可能間歇閃爍。
- 拖曳視窗時大幅旋轉手柄，移動軌跡仍可能不夠自然。
- 瀏覽器透視背景不代表已支援沉浸式 WebXR AR。

## Downloads / 下載

- [Download APK / 下載 APK](https://github.com/Stavlysa/wolvic_Vive_XR_Elite/releases/download/vive-xr-elite-wave-stage-65/Wolvic-vivexr-stage-65-v202582032.apk)
- [Complete source / 完整源碼](https://github.com/Stavlysa/wolvic_Vive_XR_Elite/releases/download/vive-xr-elite-wave-stage-65/Wolvic-vivexr-stage-65-complete-source-v202582032.zip)
- [SHA-256 checksums / 雜湊校驗](https://github.com/Stavlysa/wolvic_Vive_XR_Elite/releases/download/vive-xr-elite-wave-stage-65/SHA256SUMS.txt)

Wolvic 2.0 · Android versionCode 202582032 · package com.igalia.wolvic.

Install / 安裝：`adb install -r Wolvic-vivexr-stage-65-v202582032.apk`

The APK retains the existing local signing certificate for compatible upgrades. The source ZIP includes open-source submodules; obtain HTC Wave SDK separately.

APK 沿用既有本機簽章以保持升級相容。源碼 ZIP 包含開源子模組；HTC Wave SDK 需另外取得。
