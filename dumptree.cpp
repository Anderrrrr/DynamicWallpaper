#include <fstream>
#include <iostream>
#include <string>
#include <windows.h>


std::ofstream out("tree.txt");

void DumpLevel(HWND hwnd, int indent) {
  if (!hwnd)
    return;
  char cls[256] = {0};
  GetClassNameA(hwnd, cls, sizeof(cls));
  char txt[256] = {0};
  GetWindowTextA(hwnd, txt, sizeof(txt));
  bool vis = IsWindowVisible(hwnd);

  for (int i = 0; i < indent; i++)
    out << " ";
  out << "HWND: " << hwnd << " | Class: " << cls << " | Text: " << txt
      << " | Vis: " << vis << "\n";

  HWND child = GetWindow(hwnd, GW_CHILD);
  while (child) {
    DumpLevel(child, indent + 2);
    child = GetWindow(child, GW_HWNDNEXT);
  }
}

int main() {
  // Send 0x052C to Progman first just to replicate what DynamicWallpaper.exe
  // does
  HWND progman = FindWindow("Progman", NULL);
  DWORD_PTR res;
  SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &res);

  HWND desktop = GetWindow(GetDesktopWindow(), GW_CHILD);
  while (desktop) {
    char cls[256] = {0};
    GetClassNameA(desktop, cls, sizeof(cls));
    if (std::string(cls) == "WorkerW" || std::string(cls) == "Progman") {
      DumpLevel(desktop, 0);
    }
    desktop = GetWindow(desktop, GW_HWNDNEXT);
  }
  return 0;
}
