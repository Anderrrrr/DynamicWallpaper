# 輕量級動態桌布 (Lightweight Dynamic Wallpaper)

這是一個極輕量級的 Windows C++ 動態桌布程式，專為玩家設計的「0 效能消耗」模式方案。

## 特色
1. **底層注入**：相容 Windows 10 至目前最新 Windows 11 (24H2 / 25H2) 的桌面底層架構。
2. **多螢幕火力支援**：提供命令列參數讓您自由決定要「橫跨整個桌面」還是「每一個螢幕播放獨立畫面」。
3. **等比例裁切 (Pan and Scan)**：不論您選擇哪種模式，影片都會自動保持完美比例填滿螢幕並裁切邊緣，絕不變形。
4. **硬體解碼**：採用 Windows 內建 Media Foundation (EVR) 引擎進行 MP4 播放，不依賴龐大的外部套件如 FFmpeg，大幅度減少記憶體與 CPU 負載。
5. **無縫循環**：捕捉底層 `MEEndOfPresentation` 事件，在 EVR 清除畫面之前毫秒級 Seek 至 0 秒接續播放，避免閃爍。
6. **0 效能消耗模式**：當偵測到任何應用程式（例如遊戲或是 YouTube）處於全螢幕狀態時，影片引擎會自動發送 Pause 信號，停止繪製並釋放系統資源，確保遊戲 FPS 不掉幀；切回視窗時瞬間恢復播放。
7. **桌面圖示保留**：在 Windows 11 25H2 上透過 DefView 重定位 + 近黑色 Colorkey 透明技術，讓桌面圖示正常顯示在影片上方。

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

- **拖拉框選桌面圖示**：因 Windows `WS_EX_LAYERED` + `LWA_COLORKEY` 的設計，透明像素不接收滑鼠事件，因此無法使用拖拉框選多個桌面圖示。可使用 `Ctrl + 點擊` 替代選取多個圖示。
- **首次啟動**：程式啟動時需要重定位桌面圖示層（DefView），建議確保 Explorer 處於正常狀態。

## 開機自動啟動 (不再看到醜醜的黑窗)

如果您不想每次開機都手動輸入指令，我在資料夾裡準備了一個 `StartWallpaper.vbs` 腳本檔案。
它可以幫您在背景**無痕執行**動態桌布程式，甚至完全隱藏 CMD 黑色視窗：

1. 電腦開機後，用記事本打開 `StartWallpaper.vbs`。
2. 將腳本裡的 `VideoPath1` 變數修改為您真實的 MP4 影片路徑。
3. 如果您打算使用 `span` 或 `monitors`，請一併修改裡面的 `Mode` 變數。
4. 設定完成並存檔後，對著 `StartWallpaper.vbs` 點右鍵，選擇 **[建立捷徑]**。
5. 按下鍵盤 `Win + R` 叫出執行視窗，輸入 `shell:startup` 並按下 Enter。
6. 這個動作會打開 Windows 的「開機啟動」資料夾，請把剛剛那個「捷徑」剪下並貼進這個資料夾裡。

大功告成！未來每次您開機時，這台零效能消耗系統就會優雅地在背景為您掛上動態桌布，完全不需要您手動輸入任何指令！

## 如何關閉
1. 打開「工作管理員」。
2. 找到 `DynamicWallpaper.exe`。
3. 點擊「結束工作」即可關閉桌布並恢復原始桌面。
