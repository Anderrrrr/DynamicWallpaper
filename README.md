# 輕量級動態桌布 (Lightweight Dynamic Wallpaper)

這是一個極輕量級的 Windows C++ 動態桌布程式，專為玩家設計的「0 效能消耗」模式方案。

## 特色
1. **底層注入**：相容 Windows 10 至目前最新 Windows 11 (24H2 / 25H2) 的桌面底層架構。
2. **多螢幕火力支援**：提供命令列參數讓您自由決定要「橫跨整個桌面」還是「每一個螢幕播放獨立畫面」。
3. **等比例裁切 (Pan and Scan)**：不論您選擇哪種模式，影片都會自動保持完美比例填滿螢幕並裁切邊緣，絕不變形。
4. **純硬體解碼直出 (Zero-Copy Hardware Decoding)**：整合 `IMFDXGIDeviceManager` 讓影片引擎 (DXVA/D3D11VA) 直接輸出 `NV12` 格式的 GPU 紋理，並利用 `ID3D11VideoProcessor` 硬體色彩轉換與 Blit 到 Swapchain。CPU 使用率從原先的 ~20% 大幅降至 1~2% 範圍。
5. **無縫循環**：捕捉底層 `MEEndOfPresentation` 事件，毫秒級 Seek 至 0 秒接續播放。
6. **0 效能消耗模式**：當偵測到任何應用程式（例如遊戲或是 YouTube）處於全螢幕狀態時，影片引擎會自動暫停播放，確保遊戲 FPS 不掉幀；切回視窗時瞬間恢復。
7. **桌面圖示完美保留**：在 Windows 11 25H2 上透過 D3D11 視窗 (`WS_POPUP`) 重定位至 `Progman` 並動態置底於 `SHELLDLL_DefView` 之下，完美支援桌面圖示正常顯示與**原生的滑鼠拖曳選取 (Drag and Drop)**。

## 系統需求
- Windows 10 / Windows 11（包含 24H2 / 25H2）
- CMake (建議 3.15 以上)
- 一款 C++ 編譯器：支援 Visual Studio (MSVC) 或 MinGW (GCC)。推薦使用 MSVC 避免中文路徑字元解析問題。

## 建置與編譯教學 (使用 MSVC)

1. 確認您已經開啟了 **x64 Native Tools Command Prompt for VS 2022** 或在一般命令提示字元中載入了編譯環境變數，或者直接透過 PowerShell。
2. 開啟 PowerShell，切換到本專案資料夾底下：
   ```powershell
   cd "您的專案解壓縮路徑"
   ```
3. 產生 MSVC 的編譯設定檔：
   ```powershell
   cmake -B build_msvc
   ```
4. 開始編譯 Release 最佳化版本：
   ```powershell
   cmake --build build_msvc --config Release
   ```
5. 完成後，執行檔將會輸出至 `build_msvc\Release\DynamicWallpaper.exe`。

## 如何使用

編譯完成後，您可以打開 PowerShell 或 CMD，使用命令列參數控制多螢幕播放模式：

### 模式一：橫跨所有螢幕 (Span)
將一個影片放大並等比例裁切，橫跨您桌面上所有的螢幕：
```powershell
.\build_msvc\Release\DynamicWallpaper.exe --span "C:\Path\To\Your\Video.mp4"
```
*(如果不加任何參數，預設就是橫跨模式)*

### 模式二：每個螢幕獨立播放 (Monitors)
讓您的每一顆實體螢幕擁有獨立的影片播放器。
- **所有螢幕播放同一個影片**（影片將在每個螢幕上各自完整呈現，不會被跨螢幕裁切）：
```powershell
.\build_msvc\Release\DynamicWallpaper.exe --monitors "C:\Path\To\Your\Video.mp4"
```

- **不同螢幕播放不同影片**（例如有三個螢幕，可以傳入三個不同的影片路徑）：
```powershell
.\build_msvc\Release\DynamicWallpaper.exe --monitors "C:\影片A.mp4" "D:\影片B.mp4" "E:\影片C.mp4"
```
*(程式會依序偵測您的螢幕順序分配影片，如果提供的影片數量少於螢幕數量，後面的螢幕將會重複輪播這些影片)*


> **注意：因為此程式會針對 Progman / WorkerW 發送未公開的 Windows 底層訊息進行視窗注入，某些防毒軟體 (如 Kaspersky) 可能會將此行為誤判為惡意軟體而阻擋或刪除執行檔。若遇到此情況，請將編譯好的執行檔或專案資料夾加入防毒軟體的白名單 / 排除掃描清單中。**

## 已知限制

- **偶爾的啟動黑畫面**：由於程式啟動時需要尋找與攔截 Explorer.exe (Progman) 的底層架構，極少數情況下可能遇到未正確附著。解決方法：**直接關閉並重新啟動程式**即可。
- **偵錯日誌硬編碼**：目前 debug log 被寫死在 `C:\Users\ander\Dynamic_wallpaper\debug_log.txt`，如果路徑不存在將無法輸出日誌。

---

## 關於 Windows 11 25H2 顯示修復

在 Windows 11 版本 25H2 更新中，微軟 DWM 完全破壞了原先基於 `WS_CHILD` EVR (Enhanced Video Renderer) 渲染在桌面圖示下方的基礎機制，導致所有的動態桌面軟體均出現「全黑畫面」的嚴重錯誤。

本專案現已完全拋棄 EVR，採用了全新的 **ID3D11 + DXGI** 交換鏈 (Swapchain) 解決方案：
1. **獨立繪製視窗**：建立不受 DWM 混淆的 `WS_POPUP` Canvas。
2. **硬體轉碼通訊**：呼叫 `0x052C` 廣播分離出桌布圖層，將 D3D11 圖層父系化至 `Progman` 中。
3. **Z-Order 精確打擊**：強制將視窗排列順序 (`SetWindowPos`) 鎖定於 `SHELLDLL_DefView` 之下。

透過以上架構升級，不但繞過了 25H2 的黑畫面 Bug 阻擋，並且成功找回了所有玩家期盼的**原生滑鼠左鍵拖曳選取框功能**！
