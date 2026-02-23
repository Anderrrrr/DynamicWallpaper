Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Win32 {
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpWindowText, int nMaxCount);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);
}
"@

$global:res = @()
function DumpLevel($hwnd, $indent) {
   if ($hwnd -eq 0) { return }
   $cls = New-Object System.Text.StringBuilder 256
   [Win32]::GetClassName($hwnd, $cls, 256) | Out-Null
   $txt = New-Object System.Text.StringBuilder 256
   [Win32]::GetWindowText($hwnd, $txt, 256) | Out-Null
   $vis = [Win32]::IsWindowVisible($hwnd)
   $global:res += (" " * $indent) + "HWND: $hwnd | Class: $($cls.ToString()) | Text: $($txt.ToString()) | Vis: $vis"
   
   $child = [Win32]::GetWindow($hwnd, 5) # GW_CHILD
   while ($child -ne 0) {
      DumpLevel $child ($indent + 2)
      $child = [Win32]::GetWindow($child, 2) # GW_HWNDNEXT
   }
}

$desktop = [Win32]::GetWindow([IntPtr]::Zero, 5)
while ($desktop -ne 0) {
   $cls = New-Object System.Text.StringBuilder 256
   [Win32]::GetClassName($desktop, $cls, 256) | Out-Null
   if ($cls.ToString() -eq "WorkerW" -or $cls.ToString() -eq "Progman") {
       DumpLevel $desktop 0
   }
   $desktop = [Win32]::GetWindow($desktop, 2)
}

$global:res | Out-File -FilePath c:\DynamicWallpaperSource\tree.txt -Encoding utf8
