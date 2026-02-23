#include <iostream>
#include <windows.h>

BOOL CALLBACK EnumWindowsProcTest(HWND hwnd, LPARAM lParam) {
  char className[256];
  char windowText[256];
  GetClassNameA(hwnd, className, sizeof(className));
  GetWindowTextA(hwnd, windowText, sizeof(windowText));

  std::cout << "HWND: " << hwnd << " | Class: " << className
            << " | Text: " << windowText
            << " | Visible: " << IsWindowVisible(hwnd) << std::endl;
  return TRUE;
}

int main() {
  std::cout << "--- Visible Top Level Windows ---" << std::endl;
  EnumWindows(EnumWindowsProcTest, 0);
  return 0;
}
