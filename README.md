# 輕量級動態桌布 (Lightweight Dynamic Wallpaper)

一個極輕量的 Windows C++ 動態桌布，使用 **D3D11 GPU 解碼**直接渲染影片至桌面背景。

## 特色

| 功能 | 說明 |
|------|------|
| **GPU 硬體解碼** | 透過 Media Foundation + `IMFDXGIDeviceManager` 直接輸出 GPU 紋理，CPU 使用率 < 2% |
| **圖形管理介面 (NEW!)** | 內建 Python Flask + PyQt6 打造的現代化毛玻璃桌面 UI，支援拖曳上傳與一鍵切換桌布影片 |
| **開機自動啟動 (免 UAC) (NEW!)** | 捨棄傳統工作排程器，改採動態生成 VBS 腳本注入系統 `Startup` 資料夾。實現**免管理員權限**、無黑視窗的背景常駐，並自動記住最後播放的影片 |
| **前後端分離架構** | 底層 C++ 專注高效能渲染，控制層利用 Python Flask 提供本地 REST API，搭配 PyQt6 呈現現代化 Web UI |
| **桌面圖示保留** | 視窗注入 `Progman`，Z-Order 鎖定於 `SHELLDLL_DefView` 之下，不遮擋圖示 |
| **原生拖曳選取** | 完整支援滑鼠左鍵框選桌面圖示 (Drag and Drop) |
| **無縫循環** | 影片結束後自動 Seek 至 0 秒，無黑畫面閃爍 |
| **多螢幕展開** | 視窗跨越 Virtual Screen (`SM_CXVIRTUALSCREEN × SM_CYVIRTUALSCREEN`)，自動覆蓋所有螢幕 |
| **Windows 25H2 相容** | 捨棄舊版 EVR，改用 `WS_POPUP` + D3D11 Swapchain，修復 25H2 黑畫面問題 |

## 系統需求

| 項目 | 需求 |
|------|------|
| 作業系統 | **Windows 10 / Windows 11**（含 24H2、25H2） |
| 顯示卡 | Direct3D 11.0 以上（近十年內的獨顯或內顯均可） |
| 執行時依賴 | 無需額外安裝，D3D11 與 Media Foundation 均為 Windows 內建 |
| 編譯工具 | Visual Studio 2019 / 2022（僅編譯時需要） |

> **可以在家裡的電腦直接用！** 只要把 `DynamicWallpaper.exe` 複製過去，再準備一個影片檔，雙擊或用命令列執行即可，**不需要安裝任何執行環境**。

## 編譯方式

1. 確認已安裝 **Visual Studio 2019 / 2022**（含 C++ 桌面開發元件）。
2. 在 PowerShell 裡切換到專案根目錄，直接執行 build script：
   ```powershell
   .\build.bat
   ```
3. 編譯完成後，執行檔輸出至 `build_msvc\Release\DynamicWallpaper.exe`。

## 使用方式

### 方法一：圖形化操作介面 (推薦 ⭐)

1. 確認系統已安裝 **Python 3**。
2. 開啟 PowerShell 並安裝介面相依套件：
   ```powershell
   cd WebUI
   pip install -r requirements.txt
   pip install PyQt6 PyQt6-WebEngine opencv-python
   cd ..
   ```
3. 雙擊專案目錄下的 **`DynamicWallpaperUI.bat`** 即可開啟精美的桌面管理視窗！(也建議為它建立桌面捷徑)
   - 您可以直接把影片拖曳進去上傳。
   - 點擊「Gallery」中影片卡片的**任何地方**即可立即切換桌布。
|   - **一鍵開機自動啟動**：在左側選單進入「Settings」，將 Start on Boot 打開。程式會自動在系統啟動資料夾建立背景腳本，**完全不會觸發煩人的管理員權限 (UAC) 彈窗**。
   - **背景安靜執行**：程式由 `pythonw` 在背景無黑視窗模式下默默接管 API 伺服器與 C++ 播放引擎。若要徹底關閉，請至「工作管理員」結束 `pythonw.exe` 與 `DynamicWallpaper.exe`。
   - **自訂路徑與除錯**：支援讀取 `WebUI/config.json`，並會將執行狀況輸出至 `wallpaper_engine_log.txt` 與 `launcher.log`。

### 方法二：透過指令列 (Command Line)

```powershell
.\build_msvc\Release\DynamicWallpaper.exe "C:\Path\To\Your\Video.mp4"
```

- 支援 `.mp4`、`.mkv`、`.mov` 等 Media Foundation 可解碼的格式。
- 若未指定路徑，將嘗試尋找預設影片。
- 關閉程式：在工作列找到 `Dynamic Wallpaper` 視窗並關閉，或在 PowerShell 按 `Ctrl+C`。

## 已知限制

- **單一影片模式**：目前一次只支援一個影片播放於 Virtual Screen（橫跨所有螢幕），尚未實作每個螢幕獨立影片。
- **無音訊**：影片音軌不會播放。
- **偶爾附著失敗**：啟動時若 Progman 尚未就緒，視窗可能無法正確注入桌面層。解決方法：關閉後重新啟動程式。
- **防毒軟體誤報**：程式會對 Progman 發送未公開的 Windows 底層訊息 (`0x052C`)，部分防毒軟體（如 Kaspersky）可能誤判。請將執行檔加入白名單。

## 系統架構設計

本專案採用解耦的微服務概念設計，確保高效能與易維護性：

1. **渲染引擎 (C++ / Win32 / D3D11)**
   - 負責與 Windows 底層 `Progman` 與 `WorkerW` 視窗層級交涉，鎖定 Z-Order。
   - 透過 Media Foundation 進行 GPU 硬體解碼，並將 YUV 數據轉為 RGB 紋理繪製。
2. **控制中心與 API (Python / Flask)**
   - 在背景以 Daemon 模式運行 (`pythonw`)，提供輕量級 RESTful API (`127.0.0.1:5000`)。
   - 負責管理影片資源、生成縮圖、處理開機啟動腳本 (VBS) 的動態建立與銷毀。
3. **圖形使用者介面 (Python / PyQt6 / HTML+CSS)**
   - 透過 `QWebEngineView` 渲染精緻的前端介面，與本機 Flask API 進行非同步通訊 (AJAX/Fetch)

## 技術原理（關於修復 GPU 超速問題）

使用 D3D11 GPU 解碼時，Media Foundation 採用「共享紋理池 (Shared Texture Pool)」：解碼器不斷把新幀寫入固定的幾個 texture slot，如果程式直接持有 MF 的 texture reference 而不複製，MF 會在背景把未來幀覆寫進去——導致視覺上影片「超速」播放。

本專案的解法：每次 pop 一幀後立即用 `CopySubresourceRegion` 把影像 **blit 到我們自己的 staging texture**，然後立刻釋放 MF 的 reference，使 MF 的 texture pool 可以自由運作而不干擾顯示。

---

*本專案採用純 Win32 / D3D11 / Media Foundation 實作，無任何第三方依賴。*
