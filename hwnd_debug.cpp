#include <iostream>
#include <windows.h>


BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
  char className[256];
  char windowText[256];
  GetClassNameA(hwnd, className, sizeof(className));
  GetWindowTextA(hwnd, windowText, sizeof(windowText));

  std::cout << "  [CHILD] HWND: " << hwnd << " | Class: " << className
            << " | Text: " << windowText
            << " | Visible: " << IsWindowVisible(hwnd) << std::endl;

  return TRUE;
}

int main() {
  HWND progman = FindWindowA("Progman", NULL);
  std::cout << "[TARGET] Progman HWND: " << progman << std::endl;
  if (progman)
    EnumChildWindows(progman, EnumChildProc, 0);

  std::cout << "\n------------------\n\n";

  HWND workerW = FindWindowExA(NULL, NULL, "WorkerW", NULL);
  while (workerW) {
    std::cout << "[TARGET] WorkerW HWND: " << workerW
              << " | Visible: " << IsWindowVisible(workerW) << std::endl;
    EnumChildWindows(workerW, EnumChildProc, 0);
    workerW = FindWindowExA(NULL, workerW, "WorkerW", NULL);
  }

  return 0;
}
